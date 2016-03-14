/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_CONCURRENTMAP_GRAMPA_H
#define JUNCTION_CONCURRENTMAP_GRAMPA_H

#include <junction/Core.h>
#include <junction/details/Grampa.h>
#include <junction/QSBR.h>
#include <turf/Heap.h>
#include <turf/Trace.h>

namespace junction {

TURF_TRACE_DECLARE(ConcurrentMap_Grampa, 27)

template <typename K, typename V, class KT = DefaultKeyTraits<K>, class VT = DefaultValueTraits<V> >
class ConcurrentMap_Grampa {
public:
    typedef K Key;
    typedef V Value;
    typedef KT KeyTraits;
    typedef VT ValueTraits;
    typedef typename turf::util::BestFit<Key>::Unsigned Hash;
    typedef details::Grampa<ConcurrentMap_Grampa> Details;

private:
    turf::Atomic<uptr> m_root;

    bool locateTable(typename Details::Table*& table, ureg& sizeMask, Hash hash) {
        ureg root = m_root.load(turf::Consume);
        if (root & 1) {
            typename Details::FlatTree* flatTree = (typename Details::FlatTree*) (root & ~ureg(1));
            for (;;) {
                ureg leafIdx = ureg(hash >> flatTree->safeShift);
                table = flatTree->getTables()[leafIdx].load(turf::Relaxed);
                if (ureg(table) != Details::RedirectFlatTree) {
                    sizeMask = (Details::LeafSize - 1);
                    return true;
                }
                TURF_TRACE(ConcurrentMap_Grampa, 0, "[locateTable] flattree lookup redirected", uptr(flatTree), uptr(leafIdx));
                typename Details::FlatTreeMigration* migration = Details::getExistingFlatTreeMigration(flatTree);
                migration->run();
                migration->m_completed.wait();
                flatTree = migration->m_destination;
            }
        } else {
            if (!root)
                return false;
            table = (typename Details::Table*) root;
            sizeMask = table->sizeMask;
            return true;
        }
    }

    void createInitialTable(ureg initialSize) {
        if (!m_root.load(turf::Relaxed)) {
            // This could perform DCLI, but let's avoid needing a mutex instead.
            typename Details::Table* table = Details::Table::create(initialSize, 0, sizeof(Hash) * 8);
            if (m_root.compareExchange(uptr(NULL), uptr(table), turf::Release)) {
                TURF_TRACE(ConcurrentMap_Grampa, 1, "[createInitialTable] race to create initial table", uptr(this), 0);
                table->destroy();
            }
        }
    }

public:
    ConcurrentMap_Grampa(ureg initialSize = 0) : m_root(uptr(NULL)) {
        // FIXME: Support initialSize argument
        TURF_UNUSED(initialSize);
    }

    ~ConcurrentMap_Grampa() {
        ureg root = m_root.loadNonatomic();
        if (root & 1) {
            typename Details::FlatTree* flatTree = (typename Details::FlatTree*) (root & ~ureg(1));
            ureg size = (Hash(-1) >> flatTree->safeShift) + 1;
            typename Details::Table* lastTableGCed = NULL;
            for (ureg i = 0; i < size; i++) {
                typename Details::Table* t = flatTree->getTables()[i].loadNonatomic();
                TURF_ASSERT(ureg(t) != Details::RedirectFlatTree);
                if (t != lastTableGCed) {
                    t->destroy();
                    lastTableGCed = t;
                }
            }
            flatTree->destroy();
        } else if (root) {
            typename Details::Table* t = (typename Details::Table*) root;
            t->destroy();
        }
    }

    // publishTableMigration() is called by exactly one thread from Details::TableMigration::run()
    // after all the threads participating in the migration have completed their work.
    // There are no racing writes to the same range of hashes.
    void publishTableMigration(typename Details::TableMigration* migration) {
        TURF_TRACE(ConcurrentMap_Grampa, 2, "[publishTableMigration] called", uptr(migration), 0);
        if (migration->m_safeShift == 0) {
            // This TableMigration replaces the entire map with a single table.
            TURF_ASSERT(migration->m_baseHash == 0);
            TURF_ASSERT(migration->m_numDestinations == 1);
            ureg oldRoot = m_root.loadNonatomic(); // There are no racing writes to m_root.
            // Store the single table in m_root directly.
            typename Details::Table* newTable = migration->getDestinations()[0];
            m_root.store(uptr(newTable), turf::Release); // Make table contents visible
            newTable->isPublished.signal();
            if ((oldRoot & 1) == 0) {
                TURF_TRACE(ConcurrentMap_Grampa, 3, "[publishTableMigration] replacing single root with single root",
                           uptr(migration), 0);
                // If oldRoot is a table, it must be the original source of the migration.
                TURF_ASSERT((typename Details::Table*) oldRoot == migration->getSources()[0].table);
                // Don't GC it here. The caller will GC it since it's a source of the TableMigration.
            } else {
                TURF_TRACE(ConcurrentMap_Grampa, 4, "[publishTableMigration] replacing flattree with single root",
                           uptr(migration), 0);
                // The entire previous flattree is being replaced.
                Details::garbageCollectFlatTree((typename Details::FlatTree*) (oldRoot & ~ureg(1)));
            }
            // Caller will GC the TableMigration.
        } else {
            // We are either publishing a subtree of one or more tables, or replacing the entire map with multiple tables.
            // In either case, there will be a flattree after this function returns.
            TURF_ASSERT(migration->m_safeShift <
                        sizeof(Hash) * 8); // If m_numDestinations > 1, some index bits must remain after shifting
            ureg oldRoot = m_root.load(turf::Consume);
            if ((oldRoot & 1) == 0) {
                // There's no flattree yet. This means the TableMigration is publishing the full range of hashes.
                TURF_ASSERT(migration->m_baseHash == 0);
                TURF_ASSERT((Hash(-1) >> migration->m_safeShift) == (migration->m_numDestinations - 1));
                // The oldRoot should be the original source of the migration.
                TURF_ASSERT((typename Details::Table*) oldRoot == migration->getSources()[0].table);
                // Furthermore, it is guaranteed that there are no racing writes to m_root.
                // Create a new flattree and store it to m_root.
                TURF_TRACE(ConcurrentMap_Grampa, 5, "[publishTableMigration] replacing single root with flattree",
                           uptr(migration), 0);
                typename Details::FlatTree* flatTree = Details::FlatTree::create(migration->m_safeShift);
                typename Details::Table* prevTable = NULL;
                for (ureg i = 0; i < migration->m_numDestinations; i++) {
                    typename Details::Table* newTable = migration->getDestinations()[i];
                    flatTree->getTables()[i].storeNonatomic(newTable);
                    if (newTable != prevTable) {
                        newTable->isPublished.signal();
                        prevTable = newTable;
                    }
                }
                m_root.store(uptr(flatTree) | 1, turf::Release); // Ensure visibility of flatTree->tables
                // Caller will GC the TableMigration.
                // Caller will also GC the old oldRoot since it's a source of the TableMigration.
            } else {
                // There is an existing flattree, and we are publishing one or more tables to it.
                // Attempt to publish the subtree in a loop.
                // The loop is necessary because we might get redirected in the middle of publishing.
                TURF_TRACE(ConcurrentMap_Grampa, 6, "[publishTableMigration] publishing subtree to existing flattree",
                           uptr(migration), 0);
                typename Details::FlatTree* flatTree = (typename Details::FlatTree*) (oldRoot & ~ureg(1));
                ureg subTreeEntriesPublished = 0;
                typename Details::Table* tableToReplace = migration->getSources()[0].table;
                // Wait here so that we only replace tables that are fully published.
                // Otherwise, there will be a race between a subtree and its own children.
                // (If all ManualResetEvent objects supported isPublished(), we could add a TURF_TRACE counter for this.
                // In previous tests, such a counter does in fact get hit.)
                tableToReplace->isPublished.wait();
                typename Details::Table* prevTable = NULL;
                for (;;) {
                publishLoop:
                    if (migration->m_safeShift < flatTree->safeShift) {
                        // We'll need to migrate to larger flattree before publishing our new subtree.
                        // First, try to create a FlatTreeMigration with the necessary properties.
                        // This will fail if an existing FlatTreeMigration has already been created using the same source.
                        // In that case, we'll help complete the existing FlatTreeMigration, then we'll retry the loop.
                        TURF_TRACE(ConcurrentMap_Grampa, 7, "[publishTableMigration] existing flattree too small",
                                   uptr(migration), 0);
                        typename Details::FlatTreeMigration* flatTreeMigration =
                            Details::createFlatTreeMigration(*this, flatTree, migration->m_safeShift);
                        tableToReplace->jobCoordinator.runOne(flatTreeMigration);
                        flatTreeMigration->m_completed.wait(); // flatTreeMigration->m_destination becomes entirely visible
                        flatTree = flatTreeMigration->m_destination;
                        // The FlatTreeMigration has already been GC'ed by the last worker.
                        // Retry the loop.
                    } else {
                        ureg repeat = ureg(1) << (migration->m_safeShift - flatTree->safeShift);
                        ureg dstStartIndex = ureg(migration->m_baseHash >> flatTree->safeShift);
                        // The subtree we're about to publish fits inside the flattree.
                        TURF_ASSERT(dstStartIndex + migration->m_numDestinations * repeat - 1 <= Hash(-1) >> flatTree->safeShift);
                        // If a previous attempt to publish got redirected, resume publishing into the new flattree,
                        // starting with the first subtree entry that has not yet been fully published, as given by
                        // subTreeEntriesPublished.
                        // (Note: We could, in fact, restart the publish operation starting at entry 0. That would be valid too.
                        // We are the only thread that can modify this particular range of the flattree at this time.)
                        turf::Atomic<typename Details::Table*>* dstLeaf =
                            flatTree->getTables() + dstStartIndex + (subTreeEntriesPublished * repeat);
                        typename Details::Table** subFlatTree = migration->getDestinations();
                        while (subTreeEntriesPublished < migration->m_numDestinations) {
                            typename Details::Table* srcTable = subFlatTree[subTreeEntriesPublished];
                            for (ureg r = repeat; r > 0; r--) {
                                typename Details::Table* probeTable = tableToReplace;
                                while (!dstLeaf->compareExchangeStrong(probeTable, srcTable, turf::Relaxed)) {
                                    if (ureg(probeTable) == Details::RedirectFlatTree) {
                                        // We've been redirected.
                                        // Help with the FlatTreeMigration, then try again.
                                        TURF_TRACE(ConcurrentMap_Grampa, 8, "[publishTableMigration] redirected", uptr(migration),
                                                   uptr(dstLeaf));
                                        typename Details::FlatTreeMigration* flatTreeMigration =
                                            Details::getExistingFlatTreeMigration(flatTree);
                                        tableToReplace->jobCoordinator.runOne(flatTreeMigration);
                                        flatTreeMigration->m_completed.wait();
                                        // flatTreeMigration->m_destination becomes entirely visible
                                        flatTree = flatTreeMigration->m_destination;
                                        goto publishLoop;
                                    }
                                    // The only other possibility is that we were previously redirected, and the subtree entry got
                                    // partially published.
                                    TURF_TRACE(ConcurrentMap_Grampa, 9, "[publishTableMigration] recovering from partial publish",
                                               uptr(migration), 0);
                                    TURF_ASSERT(probeTable == srcTable);
                                }
                                // The caller will GC the table) being replaced them since it's a source of the TableMigration.
                                dstLeaf++;
                            }
                            if (prevTable != srcTable) {
                                srcTable->isPublished.signal();
                                prevTable = srcTable;
                            }
                            subTreeEntriesPublished++;
                        }
                        // We've successfully published the migrated sub-flattree.
                        // Caller will GC the TableMigration.
                        break;
                    }
                }
            }
        }
    }

    void publishFlatTreeMigration(typename Details::FlatTreeMigration* migration) {
        // There are no racing writes.
        // Old root must be the migration source (a flattree).
        TURF_ASSERT(m_root.loadNonatomic() == (ureg(migration->m_source) | 1));
        // Publish the new flattree, making entire table contents visible.
        m_root.store(uptr(migration->m_destination) | 1, turf::Release);
        // Don't GC the old flattree. The FlatTreeMigration will do that, since it's a source.
    }

    // A Mutator represents a known cell in the hash table.
    // It's meant for manipulations within a temporary function scope.
    // Obviously you must not call QSBR::Update while holding a Mutator.
    // Any operation that modifies the table (exchangeValue, eraseValue)
    // may be forced to follow a redirected cell, which changes the Mutator itself.
    // Note that even if the Mutator was constructed from an existing cell,
    // exchangeValue() can still trigger a resize if the existing cell was previously marked deleted,
    // or if another thread deletes the key between the two steps.
    class Mutator {
    private:
        friend class ConcurrentMap_Grampa;

        ConcurrentMap_Grampa& m_map;
        typename Details::Table* m_table;
        ureg m_sizeMask;
        typename Details::Cell* m_cell;
        Value m_value;

        // Constructor: Find existing cell
        Mutator(ConcurrentMap_Grampa& map, Key key, bool) : m_map(map), m_value(Value(ValueTraits::NullValue)) {
            TURF_TRACE(ConcurrentMap_Grampa, 10, "[Mutator] find constructor called", uptr(map.m_root.load(turf::Relaxed)),
                       uptr(key));
            Hash hash = KeyTraits::hash(key);
            for (;;) {
                if (!m_map.locateTable(m_table, m_sizeMask, hash))
                    return;
                m_cell = Details::find(hash, m_table, m_sizeMask);
                if (!m_cell)
                    return;
                m_value = m_cell->value.load(turf::Consume);
                if (m_value != Value(ValueTraits::Redirect))
                    return; // Found an existing value
                // We've encountered a Redirect value. Help finish the migration.
                TURF_TRACE(ConcurrentMap_Grampa, 11, "[Mutator] find was redirected", uptr(m_table), 0);
                m_table->jobCoordinator.participate();
                // Try again using the latest root.
            }
        }

        // Constructor: Insert or find cell
        Mutator(ConcurrentMap_Grampa& map, Key key) : m_map(map), m_value(Value(ValueTraits::NullValue)) {
            TURF_TRACE(ConcurrentMap_Grampa, 12, "[Mutator] insertOrFind constructor called", uptr(map.m_root.load(turf::Relaxed)),
                       uptr(key));
            Hash hash = KeyTraits::hash(key);
            for (;;) {
                if (!m_map.locateTable(m_table, m_sizeMask, hash)) {
                    m_map.createInitialTable(Details::MinTableSize);
                } else {
                    ureg overflowIdx;
                    switch (Details::insertOrFind(hash, m_table, m_sizeMask, m_cell, overflowIdx)) { // Modifies m_cell
                    case Details::InsertResult_InsertedNew: {
                        // We've inserted a new cell. Don't load m_cell->value.
                        return;
                    }
                    case Details::InsertResult_AlreadyFound: {
                        // The hash was already found in the table.
                        m_value = m_cell->value.load(turf::Consume);
                        if (m_value == Value(ValueTraits::Redirect)) {
                            // We've encountered a Redirect value.
                            TURF_TRACE(ConcurrentMap_Grampa, 13, "[Mutator] insertOrFind was redirected", uptr(m_table), uptr(m_value));
                            break; // Help finish the migration.
                        }
                        return; // Found an existing value
                    }
                    case Details::InsertResult_Overflow: {
                        Details::beginTableMigration(m_map, m_table, overflowIdx);
                        break;
                    }
                    }
                    // A migration has been started (either by us, or another thread). Participate until it's complete.
                    m_table->jobCoordinator.participate();
                }
                // Try again using the latest root.
            }
        }

    public:
        Value getValue() const {
            // Return previously loaded value. Don't load it again.
            return m_value;
        }

        Value exchangeValue(Value desired) {
            TURF_ASSERT(desired != Value(ValueTraits::NullValue));
            TURF_ASSERT(desired != Value(ValueTraits::Redirect));
            TURF_ASSERT(m_cell); // Cell must have been found or inserted
            TURF_TRACE(ConcurrentMap_Grampa, 14, "[Mutator::exchangeValue] called", uptr(m_table), uptr(m_value));
            for (;;) {
                Value oldValue = m_value;
                if (m_cell->value.compareExchangeStrong(m_value, desired, turf::ConsumeRelease)) {
                    // Exchange was successful. Return previous value.
                    TURF_TRACE(ConcurrentMap_Grampa, 15, "[Mutator::exchangeValue] exchanged Value", uptr(m_value),
                               uptr(desired));
                    Value result = m_value;
                    m_value = desired; // Leave the mutator in a valid state
                    return result;
                }
                // The CAS failed and m_value has been updated with the latest value.
                if (m_value != Value(ValueTraits::Redirect)) {
                    TURF_TRACE(ConcurrentMap_Grampa, 16, "[Mutator::exchangeValue] detected race to write value", uptr(m_table),
                               uptr(m_value));
                    if (oldValue == Value(ValueTraits::NullValue) && m_value != Value(ValueTraits::NullValue)) {
                        TURF_TRACE(ConcurrentMap_Grampa, 17, "[Mutator::exchangeValue] racing write inserted new value",
                                   uptr(m_table), uptr(m_value));
                    }
                    // There was a racing write (or erase) to this cell.
                    // Pretend we exchanged with ourselves, and just let the racing write win.
                    return desired;
                }
                // We've encountered a Redirect value. Help finish the migration.
                TURF_TRACE(ConcurrentMap_Grampa, 18, "[Mutator::exchangeValue] was redirected", uptr(m_table), uptr(m_value));
                Hash hash = m_cell->hash.load(turf::Relaxed);
                for (;;) {
                    // Help complete the migration.
                    m_table->jobCoordinator.participate();
                    // Try again in the latest table.
                    // FIXME: locateTable() could return false if the map is concurrently cleared (m_root set to 0).
                    // This is not concern yet since clear() is not implemented.
                    bool exists = m_map.locateTable(m_table, m_sizeMask, hash);
                    TURF_ASSERT(exists);
                    TURF_UNUSED(exists);
                    m_value = Value(ValueTraits::NullValue);
                    ureg overflowIdx;
                    switch (Details::insertOrFind(hash, m_table, m_sizeMask, m_cell, overflowIdx)) { // Modifies m_cell
                    case Details::InsertResult_AlreadyFound:
                        m_value = m_cell->value.load(turf::Consume);
                        if (m_value == Value(ValueTraits::Redirect)) {
                            TURF_TRACE(ConcurrentMap_Grampa, 19, "[Mutator::exchangeValue] was re-redirected", uptr(m_table),
                                       uptr(m_value));
                            break;
                        }
                        goto breakOuter;
                    case Details::InsertResult_InsertedNew:
                        goto breakOuter;
                    case Details::InsertResult_Overflow:
                        TURF_TRACE(ConcurrentMap_Grampa, 20, "[Mutator::exchangeValue] overflow after redirect", uptr(m_table),
                                   overflowIdx);
                        Details::beginTableMigration(m_map, m_table, overflowIdx);
                        break;
                    }
                    // We were redirected... again
                }
            breakOuter:;
                // Try again in the new table.
            }
        }

        void assignValue(Value desired) {
            exchangeValue(desired);
        }

        Value eraseValue() {
            TURF_ASSERT(m_cell); // Cell must have been found or inserted
            TURF_TRACE(ConcurrentMap_Grampa, 21, "[Mutator::eraseValue] called", uptr(m_table), uptr(m_value));
            for (;;) {
                if (m_value == Value(ValueTraits::NullValue))
                    return m_value;
                TURF_ASSERT(m_cell); // m_value is non-NullValue, therefore cell must have been found or inserted.
                if (m_cell->value.compareExchangeStrong(m_value, Value(ValueTraits::NullValue), turf::Consume)) {
                    // Exchange was successful and a non-NullValue value was erased and returned by reference in m_value.
                    TURF_ASSERT(m_value != Value(ValueTraits::NullValue)); // Implied by the test at the start of the loop.
                    Value result = m_value;
                    m_value = Value(ValueTraits::NullValue); // Leave the mutator in a valid state
                    return result;
                }
                // The CAS failed and m_value has been updated with the latest value.
                TURF_TRACE(ConcurrentMap_Grampa, 22, "[Mutator::eraseValue] detected race to write value", uptr(m_table),
                           uptr(m_value));
                if (m_value != Value(ValueTraits::Redirect)) {
                    // There was a racing write (or erase) to this cell.
                    // Pretend we erased nothing, and just let the racing write win.
                    return Value(ValueTraits::NullValue);
                }
                // We've been redirected to a new table.
                TURF_TRACE(ConcurrentMap_Grampa, 23, "[Mutator::eraseValue] was redirected", uptr(m_table), uptr(m_cell));
                Hash hash = m_cell->hash.load(turf::Relaxed); // Re-fetch hash
                for (;;) {
                    // Help complete the migration.
                    m_table->jobCoordinator.participate();
                    // Try again in the latest table.
                    if (!m_map.locateTable(m_table, m_sizeMask, hash))
                        m_cell = NULL;
                    else
                        m_cell = Details::find(hash, m_table, m_sizeMask);
                    if (!m_cell) {
                        m_value = Value(ValueTraits::NullValue);
                        return m_value;
                    }
                    m_value = m_cell->value.load(turf::Relaxed);
                    if (m_value != Value(ValueTraits::Redirect))
                        break;
                    TURF_TRACE(ConcurrentMap_Grampa, 24, "[Mutator::eraseValue] was re-redirected", uptr(m_table), uptr(m_cell));
                }
            }
        }
    };

    Mutator insertOrFind(Key key) {
        return Mutator(*this, key);
    }

    Mutator find(Key key) {
        return Mutator(*this, key, false);
    }

    // Lookup without creating a temporary Mutator.
    Value get(Key key) {
        Hash hash = KeyTraits::hash(key);
        TURF_TRACE(ConcurrentMap_Grampa, 25, "[get] called", uptr(this), uptr(hash));
        for (;;) {
            typename Details::Table* table;
            ureg sizeMask;
            if (!locateTable(table, sizeMask, hash))
                return Value(ValueTraits::NullValue);
            typename Details::Cell* cell = Details::find(hash, table, sizeMask);
            if (!cell)
                return Value(ValueTraits::NullValue);
            Value value = cell->value.load(turf::Consume);
            if (value != Value(ValueTraits::Redirect))
                return value; // Found an existing value
            // We've been redirected to a new table. Help with the migration.
            TURF_TRACE(ConcurrentMap_Grampa, 26, "[get] was redirected", uptr(table), 0);
            table->jobCoordinator.participate();
            // Try again in the new table.
        }
    }

    Value assign(Key key, Value desired) {
        Mutator iter(*this, key);
        return iter.exchangeValue(desired);
    }

    Value exchange(Key key, Value desired) {
        Mutator iter(*this, key);
        return iter.exchangeValue(desired);
    }

    Value erase(Key key) {
        Mutator iter(*this, key, false);
        return iter.eraseValue();
    }

    // The easiest way to implement an Iterator is to prevent all Redirects.
    // The currrent Iterator does that by forbidding concurrent inserts.
    // To make it work with concurrent inserts, we'd need a way to block TableMigrations as the Iterator visits each table.
    // FlatTreeMigrations, too.
    class Iterator {
    private:
        typename Details::FlatTree* m_flatTree;
        ureg m_flatTreeIdx;
        typename Details::Table* m_table;
        ureg m_idx;
        Key m_hash;
        Value m_value;

    public:
        Iterator(ConcurrentMap_Grampa& map) {
            // Since we've forbidden concurrent inserts (for now), nonatomic would suffice here, but let's plan ahead:
            ureg root = map.m_root.load(turf::Consume);
            if (root & 1) {
                m_flatTree = (typename Details::FlatTree*) (root & ~ureg(1));
                TURF_ASSERT(m_flatTree->getSize() > 0);
                m_flatTreeIdx = 0;
                m_table = m_flatTree->getTables()[0].load(turf::Consume);
                TURF_ASSERT(m_table);
                m_idx = -1;
            } else {
                m_flatTree = NULL;
                m_flatTreeIdx = 0;
                m_table = (typename Details::Table*) root;
                m_idx = -1;
            }
            if (m_table) {
                next();
            } else {
                m_hash = KeyTraits::NullHash;
                m_value = Value(ValueTraits::NullValue);
            }
        }

        void next() {
            TURF_ASSERT(m_table);
            TURF_ASSERT(isValid() || m_idx == -1); // Either the Iterator is already valid, or we've just started iterating.
            for (;;) {
            searchInTable:
                m_idx++;
                if (m_idx <= m_table->sizeMask) {
                    // Index still inside range of table.
                    typename Details::CellGroup* group = m_table->getCellGroups() + (m_idx >> 2);
                    typename Details::Cell* cell = group->cells + (m_idx & 3);
                    m_hash = cell->hash.load(turf::Relaxed);
                    if (m_hash != KeyTraits::NullHash) {
                        // Cell has been reserved.
                        m_value = cell->value.load(turf::Relaxed);
                        TURF_ASSERT(m_value != Value(ValueTraits::Redirect));
                        if (m_value != Value(ValueTraits::NullValue))
                            return; // Yield this cell.
                    }
                } else {
                    // We've advanced past the end of this table.
                    if (m_flatTree) {
                        // Scan for the next unique table in the flattree.
                        while (++m_flatTreeIdx < m_flatTree->getSize()) {
                            typename Details::Table* nextTable = m_flatTree->getTables()[m_flatTreeIdx].load(turf::Consume);
                            if (nextTable != m_table) {
                                // Found the next table.
                                m_table = nextTable;
                                m_idx = -1;
                                goto searchInTable; // Continue iterating in this table.
                            }
                        }
                    }
                    // That's the end of the entire map.
                    m_hash = KeyTraits::NullHash;
                    m_value = Value(ValueTraits::NullValue);
                    return;
                }
            }
        }

        bool isValid() const {
            return m_value != Value(ValueTraits::NullValue);
        }

        Key getKey() const {
            TURF_ASSERT(isValid());
            // Since we've forbidden concurrent inserts (for now), nonatomic would suffice here, but let's plan ahead:
            return KeyTraits::dehash(m_hash);
        }

        Value getValue() const {
            TURF_ASSERT(isValid());
            return m_value;
        }
    };
};

} // namespace junction

#endif // JUNCTION_CONCURRENTMAP_GRAMPA_H
