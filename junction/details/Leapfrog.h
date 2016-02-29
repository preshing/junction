/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_DETAILS_LEAPFROG_H
#define JUNCTION_DETAILS_LEAPFROG_H

#include <junction/Core.h>
#include <turf/Atomic.h>
#include <turf/Mutex.h>
#include <turf/ManualResetEvent.h>
#include <turf/Util.h>
#include <junction/MapTraits.h>
#include <turf/Trace.h>
#include <turf/Heap.h>
#include <junction/SimpleJobCoordinator.h>
#include <junction/QSBR.h>

namespace junction {
namespace details {

TURF_TRACE_DECLARE(Leapfrog, 33)

template <class Map>
struct Leapfrog {
    typedef typename Map::Hash Hash;
    typedef typename Map::Value Value;
    typedef typename Map::KeyTraits KeyTraits;
    typedef typename Map::ValueTraits ValueTraits;

    static const ureg InitialSize = 8;
    static const ureg TableMigrationUnitSize = 32;
    static const ureg LinearSearchLimit = 128;
    static const ureg CellsInUseSample = LinearSearchLimit;
    TURF_STATIC_ASSERT(LinearSearchLimit > 0 && LinearSearchLimit < 256);              // Must fit in CellGroup::links
    TURF_STATIC_ASSERT(CellsInUseSample > 0 && CellsInUseSample <= LinearSearchLimit); // Limit sample to failed search chain

    struct Cell {
        turf::Atomic<Hash> hash;
        turf::Atomic<Value> value;
    };

    struct CellGroup {
        // Every cell in the table actually represents a bucket of cells, all linked together in a probe chain.
        // Each cell in the probe chain is located within the table itself.
        // "deltas" determines the index of the next cell in the probe chain.
        // The first cell in the chain is the one that was hashed. It may or may not actually belong in the bucket.
        // The "second" cell in the chain is given by deltas 0 - 3. It's guaranteed to belong in the bucket.
        // All subsequent cells in the chain is given by deltas 4 - 7. Also guaranteed to belong in the bucket.
        turf::Atomic<u8> deltas[8];
        Cell cells[4];
    };

    struct Table {
        const ureg sizeMask;                 // a power of two minus one
        turf::Mutex mutex;                   // to DCLI the TableMigration (stored in the jobCoordinator)
        SimpleJobCoordinator jobCoordinator; // makes all blocked threads participate in the migration

        Table(ureg sizeMask) : sizeMask(sizeMask) {
        }

        static Table* create(ureg tableSize) {
            TURF_ASSERT(turf::util::isPowerOf2(tableSize));
            TURF_ASSERT(tableSize >= 4);
            ureg numGroups = tableSize >> 2;
            Table* table = (Table*) TURF_HEAP.alloc(sizeof(Table) + sizeof(CellGroup) * numGroups);
            new (table) Table(tableSize - 1);
            for (ureg i = 0; i < numGroups; i++) {
                CellGroup* group = table->getCellGroups() + i;
                for (ureg j = 0; j < 4; j++) {
                    group->deltas[j].storeNonatomic(0);
                    group->deltas[j + 4].storeNonatomic(0);
                    group->cells[j].hash.storeNonatomic(KeyTraits::NullHash);
                    group->cells[j].value.storeNonatomic(Value(ValueTraits::NullValue));
                }
            }
            return table;
        }

        void destroy() {
            this->Table::~Table();
            TURF_HEAP.free(this);
        }

        CellGroup* getCellGroups() const {
            return (CellGroup*) (this + 1);
        }

        ureg getNumMigrationUnits() const {
            return sizeMask / TableMigrationUnitSize + 1;
        }
    };

    class TableMigration : public SimpleJobCoordinator::Job {
    public:
        struct Source {
            Table* table;
            turf::Atomic<ureg> sourceIndex;
        };

        Map& m_map;
        Table* m_destination;
        turf::Atomic<ureg> m_workerStatus; // number of workers + end flag
        turf::Atomic<bool> m_overflowed;
        turf::Atomic<sreg> m_unitsRemaining;
        ureg m_numSources;

        TableMigration(Map& map) : m_map(map) {
        }

        static TableMigration* create(Map& map, ureg numSources) {
            TableMigration* migration =
                (TableMigration*) TURF_HEAP.alloc(sizeof(TableMigration) + sizeof(TableMigration::Source) * numSources);
            new (migration) TableMigration(map);
            migration->m_workerStatus.storeNonatomic(0);
            migration->m_overflowed.storeNonatomic(false);
            migration->m_unitsRemaining.storeNonatomic(0);
            migration->m_numSources = numSources;
            // Caller is responsible for filling in sources & destination
            return migration;
        }

        virtual ~TableMigration() TURF_OVERRIDE {
        }

        void destroy() {
            // Destroy all source tables.
            for (ureg i = 0; i < m_numSources; i++)
                if (getSources()[i].table)
                    getSources()[i].table->destroy();
            // Delete the migration object itself.
            this->TableMigration::~TableMigration();
            TURF_HEAP.free(this);
        }

        Source* getSources() const {
            return (Source*) (this + 1);
        }

        bool migrateRange(Table* srcTable, ureg startIdx);
        virtual void run() TURF_OVERRIDE;
    };

    static Cell* find(Hash hash, Table* table) {
        TURF_TRACE(Leapfrog, 0, "[find] called", uptr(table), hash);
        TURF_ASSERT(table);
        TURF_ASSERT(hash != KeyTraits::NullHash);
        ureg sizeMask = table->sizeMask;
        // Optimistically check hashed cell even though it might belong to another bucket
        ureg idx = hash & sizeMask;
        CellGroup* group = table->getCellGroups() + (idx >> 2);
        Cell* cell = group->cells + (idx & 3);
        Hash probeHash = cell->hash.load(turf::Relaxed);
        if (probeHash == hash) {
            TURF_TRACE(Leapfrog, 1, "[find] found existing cell optimistically", uptr(table), idx);
            return cell;
        } else if (probeHash == KeyTraits::NullHash) {
            return cell = NULL;
        }
        // Follow probe chain for our bucket
        u8 delta = group->deltas[idx & 3].load(turf::Relaxed);
        while (delta) {
            idx = (idx + delta) & sizeMask;
            group = table->getCellGroups() + (idx >> 2);
            cell = group->cells + (idx & 3);
            Hash probeHash = cell->hash.load(turf::Relaxed);
            // Note: probeHash might actually be NULL due to memory reordering of a concurrent insert,
            // but we don't check for it. We just follow the probe chain.
            if (probeHash == hash) {
                TURF_TRACE(Leapfrog, 2, "[find] found existing cell", uptr(table), idx);
                return cell;
            }
            delta = group->deltas[(idx & 3) + 4].load(turf::Relaxed);
        }
        // End of probe chain, not found
        return NULL;
    }

    // FIXME: Possible optimization: Dedicated insert for migration? It wouldn't check for InsertResult_AlreadyFound.
    enum InsertResult { InsertResult_AlreadyFound, InsertResult_InsertedNew, InsertResult_Overflow };
    static InsertResult insertOrFind(Hash hash, Table* table, Cell*& cell, ureg& overflowIdx) {
        TURF_TRACE(Leapfrog, 3, "[insertOrFind] called", uptr(table), hash);
        TURF_ASSERT(table);
        TURF_ASSERT(hash != KeyTraits::NullHash);
        ureg sizeMask = table->sizeMask;
        ureg idx = ureg(hash);

        // Check hashed cell first, though it may not even belong to the bucket.
        CellGroup* group = table->getCellGroups() + ((idx & sizeMask) >> 2);
        cell = group->cells + (idx & 3);
        Hash probeHash = cell->hash.load(turf::Relaxed);
        if (probeHash == KeyTraits::NullHash) {
            if (cell->hash.compareExchangeStrong(probeHash, hash, turf::Relaxed)) {
                TURF_TRACE(Leapfrog, 4, "[insertOrFind] reserved first cell", uptr(table), idx);
                // There are no links to set. We're done.
                return InsertResult_InsertedNew;
            } else {
                TURF_TRACE(Leapfrog, 5, "[insertOrFind] race to reserve first cell", uptr(table), idx);
                // Fall through to check if it was the same hash...
            }
        }
        if (probeHash == hash) {
            TURF_TRACE(Leapfrog, 6, "[insertOrFind] found in first cell", uptr(table), idx);
            return InsertResult_AlreadyFound;
        }

        // Follow the link chain for this bucket.
        ureg maxIdx = idx + sizeMask;
        ureg linkLevel = 0;
        turf::Atomic<u8>* prevLink;
        for (;;) {
        followLink:
            prevLink = group->deltas + ((idx & 3) + linkLevel);
            linkLevel = 4;
            u8 probeDelta = prevLink->load(turf::Relaxed);
            if (probeDelta) {
                idx += probeDelta;
                // Check the hash for this cell.
                group = table->getCellGroups() + ((idx & sizeMask) >> 2);
                cell = group->cells + (idx & 3);
                probeHash = cell->hash.load(turf::Relaxed);
                if (probeHash == KeyTraits::NullHash) {
                    // Cell was linked, but hash is not visible yet.
                    // We could avoid this case (and guarantee it's visible) using acquire & release, but instead,
                    // just poll until it becomes visible.
                    TURF_TRACE(Leapfrog, 7, "[insertOrFind] race to read hash", uptr(table), idx);
                    do {
                        probeHash = cell->hash.load(turf::Acquire);
                    } while (probeHash == KeyTraits::NullHash);
                }
                TURF_ASSERT(((probeHash ^ hash) & sizeMask) == 0); // Only hashes in same bucket can be linked
                if (probeHash == hash) {
                    TURF_TRACE(Leapfrog, 8, "[insertOrFind] found in probe chain", uptr(table), idx);
                    return InsertResult_AlreadyFound;
                }
            } else {
                // Reached the end of the link chain for this bucket.
                // Switch to linear probing until we reserve a new cell or find a late-arriving cell in the same bucket.
                ureg prevLinkIdx = idx;
                TURF_ASSERT(sreg(maxIdx - idx) >= 0); // Nobody would have linked an idx that's out of range.
                ureg linearProbesRemaining = turf::util::min(maxIdx - idx, LinearSearchLimit);
                while (linearProbesRemaining-- > 0) {
                    idx++;
                    group = table->getCellGroups() + ((idx & sizeMask) >> 2);
                    cell = group->cells + (idx & 3);
                    probeHash = cell->hash.load(turf::Relaxed);
                    if (probeHash == KeyTraits::NullHash) {
                        // It's an empty cell. Try to reserve it.
                        if (cell->hash.compareExchangeStrong(probeHash, hash, turf::Relaxed)) {
                            // Success. We've reserved the cell. Link it to previous cell in same bucket.
                            TURF_TRACE(Leapfrog, 9, "[insertOrFind] reserved cell", uptr(table), idx);
                            TURF_ASSERT(probeDelta == 0);
                            u8 desiredDelta = idx - prevLinkIdx;
#if TURF_WITH_ASSERTS
                            probeDelta = prevLink->exchange(desiredDelta, turf::Relaxed);
                            TURF_ASSERT(probeDelta == 0 || probeDelta == desiredDelta);
#else
                            prevLink->store(desiredDelta, turf::Relaxed);
#endif
                            return InsertResult_InsertedNew;
                        } else {
                            TURF_TRACE(Leapfrog, 10, "[insertOrFind] race to reserve cell", uptr(table), idx);
                            // Fall through to check if it's the same hash...
                        }
                    }
                    Hash x = (probeHash ^ hash);
                    // Check for same hash.
                    if (!x) {
                        TURF_TRACE(Leapfrog, 11, "[insertOrFind] found outside probe chain", uptr(table), idx);
                        return InsertResult_AlreadyFound;
                    }
                    // Check for same bucket.
                    if ((x & sizeMask) == 0) {
                        TURF_TRACE(Leapfrog, 12, "[insertOrFind] found late-arriving cell in same bucket", uptr(table), idx);
                        // Attempt to set the link on behalf of the late-arriving cell.
                        // This is usually redundant, but if we don't attempt to set the late-arriving cell's link here,
                        // there's no guarantee that our own link chain will be well-formed by the time this function returns.
                        // (Indeed, subsequent lookups sometimes failed during testing, for this exact reason.)
                        u8 desiredDelta = idx - prevLinkIdx;
#if TURF_WITH_ASSERTS
                        probeDelta = prevLink->exchange(desiredDelta, turf::Relaxed);
                        TURF_ASSERT(probeDelta == 0 || probeDelta == desiredDelta);
                        if (probeDelta == 0)
                            TURF_TRACE(Leapfrog, 13, "[insertOrFind] set link on behalf of late-arriving cell", uptr(table), idx);
#else
                        prevLink->store(desiredDelta, turf::Relaxed);
#endif
                        goto followLink; // Try to follow link chain for the bucket again.
                    }
                    // Continue linear search...
                }
                // Table is too full to insert.
                overflowIdx = idx + 1;
                TURF_TRACE(Leapfrog, 14, "[insertOrFind] overflow", uptr(table), overflowIdx);
                return InsertResult_Overflow;
            }
        }
    }

    static void beginTableMigrationToSize(Map& map, Table* table, ureg nextTableSize) {
        // Create new migration by DCLI.
        TURF_TRACE(Leapfrog, 15, "[beginTableMigrationToSize] called", 0, 0);
        SimpleJobCoordinator::Job* job = table->jobCoordinator.loadConsume();
        if (job) {
            TURF_TRACE(Leapfrog, 16, "[beginTableMigrationToSize] new migration already exists", 0, 0);
        } else {
            turf::LockGuard<turf::Mutex> guard(table->mutex);
            job = table->jobCoordinator.loadConsume(); // Non-atomic would be sufficient, but that's OK.
            if (job) {
                TURF_TRACE(Leapfrog, 17, "[beginTableMigrationToSize] new migration already exists (double-checked)", 0, 0);
            } else {
                // Create new migration.
                TableMigration* migration = TableMigration::create(map, 1);
                migration->m_unitsRemaining.storeNonatomic(table->getNumMigrationUnits());
                migration->getSources()[0].table = table;
                migration->getSources()[0].sourceIndex.storeNonatomic(0);
                migration->m_destination = Table::create(nextTableSize);
                // Publish the new migration.
                table->jobCoordinator.storeRelease(migration);
            }
        }
    }

    static void beginTableMigration(Map& map, Table* table, ureg overflowIdx) {
        // Estimate number of cells in use based on a small sample.
        ureg sizeMask = table->sizeMask;
        ureg idx = overflowIdx - CellsInUseSample;
        ureg inUseCells = 0;
        for (ureg linearProbesRemaining = CellsInUseSample; linearProbesRemaining > 0; linearProbesRemaining--) {
            CellGroup* group = table->getCellGroups() + ((idx & sizeMask) >> 2);
            Cell* cell = group->cells + (idx & 3);
            Value value = cell->value.load(turf::Relaxed);
            if (value == Value(ValueTraits::Redirect)) {
                // Another thread kicked off the jobCoordinator. The caller will participate upon return.
                TURF_TRACE(Leapfrog, 18, "[beginTableMigration] redirected while determining table size", 0, 0);
                return;
            }
            if (value != Value(ValueTraits::NullValue))
                inUseCells++;
            idx++;
        }
        float inUseRatio = float(inUseCells) / CellsInUseSample;
        float estimatedInUse = (sizeMask + 1) * inUseRatio;
#if JUNCTION_LEAPFROG_FORCE_MIGRATION_OVERFLOWS
        // Periodically underestimate the number of cells in use.
        // This exercises the code that handles overflow during migration.
        static ureg counter = 1;
        if ((++counter & 3) == 0) {
            estimatedInUse /= 4;
        }
#endif
        ureg nextTableSize = turf::util::max(InitialSize, turf::util::roundUpPowerOf2(ureg(estimatedInUse * 2)));
        beginTableMigrationToSize(map, table, nextTableSize);
    }
}; // Leapfrog

template <class Map>
bool Leapfrog<Map>::TableMigration::migrateRange(Table* srcTable, ureg startIdx) {
    ureg srcSizeMask = srcTable->sizeMask;
    ureg endIdx = turf::util::min(startIdx + TableMigrationUnitSize, srcSizeMask + 1);
    // Iterate over source range.
    for (ureg srcIdx = startIdx; srcIdx < endIdx; srcIdx++) {
        CellGroup* srcGroup = srcTable->getCellGroups() + ((srcIdx & srcSizeMask) >> 2);
        Cell* srcCell = srcGroup->cells + (srcIdx & 3);
        Hash srcHash;
        Value srcValue;
        // Fetch the srcHash and srcValue.
        for (;;) {
            srcHash = srcCell->hash.load(turf::Relaxed);
            if (srcHash == KeyTraits::NullHash) {
                // An unused cell. Try to put a Redirect marker in its value.
                srcValue =
                    srcCell->value.compareExchange(Value(ValueTraits::NullValue), Value(ValueTraits::Redirect), turf::Relaxed);
                if (srcValue == Value(ValueTraits::Redirect)) {
                    // srcValue is already marked Redirect due to previous incomplete migration.
                    TURF_TRACE(Leapfrog, 19, "[migrateRange] empty cell already redirected", uptr(srcTable), srcIdx);
                    break;
                }
                if (srcValue == Value(ValueTraits::NullValue))
                    break; // Redirect has been placed. Break inner loop, continue outer loop.
                TURF_TRACE(Leapfrog, 20, "[migrateRange] race to insert key", uptr(srcTable), srcIdx);
                // Otherwise, somebody just claimed the cell. Read srcHash again...
            } else {
                // Check for deleted/uninitialized value.
                srcValue = srcCell->value.load(turf::Relaxed);
                if (srcValue == Value(ValueTraits::NullValue)) {
                    // Try to put a Redirect marker.
                    if (srcCell->value.compareExchangeStrong(srcValue, Value(ValueTraits::Redirect), turf::Relaxed))
                        break; // Redirect has been placed. Break inner loop, continue outer loop.
                    TURF_TRACE(Leapfrog, 21, "[migrateRange] race to insert value", uptr(srcTable), srcIdx);
                    if (srcValue == Value(ValueTraits::Redirect)) {
                        // FIXME: I don't think this will happen. Investigate & change to assert
                        TURF_TRACE(Leapfrog, 22, "[migrateRange] race inserted Redirect", uptr(srcTable), srcIdx);
                        break;
                    }
                } else if (srcValue == Value(ValueTraits::Redirect)) {
                    // srcValue is already marked Redirect due to previous incomplete migration.
                    TURF_TRACE(Leapfrog, 23, "[migrateRange] in-use cell already redirected", uptr(srcTable), srcIdx);
                    break;
                }

                // We've got a key/value pair to migrate.
                // Reserve a destination cell in the destination.
                TURF_ASSERT(srcHash != KeyTraits::NullHash);
                TURF_ASSERT(srcValue != Value(ValueTraits::NullValue));
                TURF_ASSERT(srcValue != Value(ValueTraits::Redirect));
                Cell* dstCell;
                ureg overflowIdx;
                InsertResult result = insertOrFind(srcHash, m_destination, dstCell, overflowIdx);
                // During migration, a hash can only exist in one place among all the source tables,
                // and it is only migrated by one thread. Therefore, the hash will never already exist
                // in the destination table:
                TURF_ASSERT(result != InsertResult_AlreadyFound);
                if (result == InsertResult_Overflow) {
                    // Destination overflow.
                    // This can happen for several reasons. For example, the source table could have
                    // existed of all deleted cells when it overflowed, resulting in a small destination
                    // table size, but then another thread could re-insert all the same hashes
                    // before the migration completed.
                    // Caller will cancel the current migration and begin a new one.
                    return false;
                }
                // Migrate the old value to the new cell.
                for (;;) {
                    // Copy srcValue to the destination.
                    dstCell->value.store(srcValue, turf::Relaxed);
                    // Try to place a Redirect marker in srcValue.
                    Value doubleCheckedSrcValue =
                        srcCell->value.compareExchange(srcValue, Value(ValueTraits::Redirect), turf::Relaxed);
                    TURF_ASSERT(doubleCheckedSrcValue !=
                                Value(ValueTraits::Redirect)); // Only one thread can redirect a cell at a time.
                    if (doubleCheckedSrcValue == srcValue) {
                        // No racing writes to the src. We've successfully placed the Redirect marker.
                        // srcValue was non-NULL when we decided to migrate it, but it may have changed to NULL
                        // by a late-arriving erase.
                        if (srcValue == Value(ValueTraits::NullValue))
                            TURF_TRACE(Leapfrog, 24, "[migrateRange] racing update was erase", uptr(srcTable), srcIdx);
                        break;
                    }
                    // There was a late-arriving write (or erase) to the src. Migrate the new value and try again.
                    TURF_TRACE(Leapfrog, 25, "[migrateRange] race to update migrated value", uptr(srcTable), srcIdx);
                    srcValue = doubleCheckedSrcValue;
                }
                // Cell successfully migrated. Proceed to next source cell.
                break;
            }
        }
    }
    // Range has been migrated successfully.
    return true;
}

template <class Map>
void Leapfrog<Map>::TableMigration::run() {
    // Conditionally increment the shared # of workers.
    ureg probeStatus = m_workerStatus.load(turf::Relaxed);
    do {
        if (probeStatus & 1) {
            // End flag is already set, so do nothing.
            TURF_TRACE(Leapfrog, 26, "[TableMigration::run] already ended", uptr(this), 0);
            return;
        }
    } while (!m_workerStatus.compareExchangeWeak(probeStatus, probeStatus + 2, turf::Relaxed, turf::Relaxed));
    // # of workers has been incremented, and the end flag is clear.
    TURF_ASSERT((probeStatus & 1) == 0);

    // Iterate over all source tables.
    for (ureg s = 0; s < m_numSources; s++) {
        Source& source = getSources()[s];
        // Loop over all migration units in this source table.
        for (;;) {
            if (m_workerStatus.load(turf::Relaxed) & 1) {
                TURF_TRACE(Leapfrog, 27, "[TableMigration::run] detected end flag set", uptr(this), 0);
                goto endMigration;
            }
            ureg startIdx = source.sourceIndex.fetchAdd(TableMigrationUnitSize, turf::Relaxed);
            if (startIdx >= source.table->sizeMask + 1)
                break; // No more migration units in this table. Try next source table.
            bool overflowed = !migrateRange(source.table, startIdx);
            if (overflowed) {
                // *** FAILED MIGRATION ***
                // TableMigration failed due to destination table overflow.
                // No other thread can declare the migration successful at this point, because *this* unit will never complete,
                // hence m_unitsRemaining won't reach zero.
                // However, multiple threads can independently detect a failed migration at the same time.
                TURF_TRACE(Leapfrog, 28, "[TableMigration::run] destination overflow", uptr(source.table), uptr(startIdx));
                // The reason we store overflowed in a shared variable is because we can must flush all the worker threads before
                // we can safely deal with the overflow. Therefore, the thread that detects the failure is often different from
                // the thread
                // that deals with it.
                bool oldOverflowed = m_overflowed.exchange(overflowed, turf::Relaxed);
                if (oldOverflowed)
                    TURF_TRACE(Leapfrog, 29, "[TableMigration::run] race to set m_overflowed", uptr(overflowed),
                               uptr(oldOverflowed));
                m_workerStatus.fetchOr(1, turf::Relaxed);
                goto endMigration;
            }
            sreg prevRemaining = m_unitsRemaining.fetchSub(1, turf::Relaxed);
            TURF_ASSERT(prevRemaining > 0);
            if (prevRemaining == 1) {
                // *** SUCCESSFUL MIGRATION ***
                // That was the last chunk to migrate.
                m_workerStatus.fetchOr(1, turf::Relaxed);
                goto endMigration;
            }
        }
    }
    TURF_TRACE(Leapfrog, 30, "[TableMigration::run] out of migration units", uptr(this), 0);

endMigration:
    // Decrement the shared # of workers.
    probeStatus = m_workerStatus.fetchSub(
        2, turf::AcquireRelease); // AcquireRelease makes all previous writes visible to the last worker thread.
    if (probeStatus >= 4) {
        // There are other workers remaining. Return here so that only the very last worker will proceed.
        TURF_TRACE(Leapfrog, 31, "[TableMigration::run] not the last worker", uptr(this), uptr(probeStatus));
        return;
    }

    // We're the very last worker thread.
    // Perform the appropriate post-migration step depending on whether the migration succeeded or failed.
    TURF_ASSERT(probeStatus == 3);
    bool overflowed = m_overflowed.loadNonatomic(); // No racing writes at this point
    if (!overflowed) {
        // The migration succeeded. This is the most likely outcome. Publish the new subtree.
        m_map.publishTableMigration(this);
        // End the jobCoodinator.
        getSources()[0].table->jobCoordinator.end();
    } else {
        // The migration failed due to the overflow of the destination table.
        Table* origTable = getSources()[0].table;
        turf::LockGuard<turf::Mutex> guard(origTable->mutex);
        SimpleJobCoordinator::Job* checkedJob = origTable->jobCoordinator.loadConsume();
        if (checkedJob != this) {
            TURF_TRACE(Leapfrog, 32, "[TableMigration::run] a new TableMigration was already started", uptr(origTable),
                       uptr(checkedJob));
        } else {
            TableMigration* migration = TableMigration::create(m_map, m_numSources + 1);
            // Double the destination table size.
            migration->m_destination = Table::create((m_destination->sizeMask + 1) * 2);
            // Transfer source tables to the new migration.
            for (ureg i = 0; i < m_numSources; i++) {
                migration->getSources()[i].table = getSources()[i].table;
                getSources()[i].table = NULL;
                migration->getSources()[i].sourceIndex.storeNonatomic(0);
            }
            migration->getSources()[m_numSources].table = m_destination;
            migration->getSources()[m_numSources].sourceIndex.storeNonatomic(0);
            // Calculate total number of migration units to move.
            ureg unitsRemaining = 0;
            for (ureg s = 0; s < migration->m_numSources; s++)
                unitsRemaining += migration->getSources()[s].table->getNumMigrationUnits();
            migration->m_unitsRemaining.storeNonatomic(unitsRemaining);
            // Publish the new migration.
            origTable->jobCoordinator.storeRelease(migration);
        }
    }

    // We're done with this TableMigration. Queue it for GC.
    DefaultQSBR.enqueue(&TableMigration::destroy, this);
}

} // namespace details
} // namespace junction

#endif // JUNCTION_DETAILS_LEAPFROG_H
