/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_CONCURRENTMAP_LINEAR_H
#define JUNCTION_CONCURRENTMAP_LINEAR_H

#include <junction/Core.h>
#include <junction/details/Linear.h>
#include <junction/QSBR.h>
#include <turf/Heap.h>
#include <turf/Trace.h>

namespace junction {

TURF_TRACE_DECLARE(ConcurrentMap_Linear, 17)

template <typename K, typename V, class KT = DefaultKeyTraits<K>, class VT = DefaultValueTraits<V> >
class ConcurrentMap_Linear {
public:
    typedef K Key;
    typedef V Value;
    typedef KT KeyTraits;
    typedef VT ValueTraits;
    typedef typename turf::util::BestFit<Key>::Unsigned Hash;
    typedef details::Linear<ConcurrentMap_Linear> Details;

private:
    turf::Atomic<typename Details::Table*> m_root;

public:
    ConcurrentMap_Linear(ureg capacity = Details::InitialSize) : m_root(Details::Table::create(capacity)) {
    }

    ~ConcurrentMap_Linear() {
        typename Details::Table* table = m_root.loadNonatomic();
        table->destroy();
    }

    // publishTableMigration() is called by exactly one thread from Details::TableMigration::run()
    // after all the threads participating in the migration have completed their work.
    void publishTableMigration(typename Details::TableMigration* migration) {
        // There are no racing calls to this function.
        typename Details::Table* oldRoot = m_root.loadNonatomic();
        m_root.store(migration->m_destination, turf::Release);
        TURF_ASSERT(oldRoot == migration->getSources()[0].table);
        // Caller will GC the TableMigration and the source table.
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
        friend class ConcurrentMap_Linear;

        ConcurrentMap_Linear& m_map;
        typename Details::Table* m_table;
        typename Details::Cell* m_cell;
        Value m_value;

        // Constructor: Find existing cell
        Mutator(ConcurrentMap_Linear& map, Key key, bool) : m_map(map), m_value(Value(ValueTraits::NullValue)) {
            TURF_TRACE(ConcurrentMap_Linear, 0, "[Mutator] find constructor called", uptr(0), uptr(key));
            Hash hash = KeyTraits::hash(key);
            for (;;) {
                m_table = m_map.m_root.load(turf::Consume);
                m_cell = Details::find(hash, m_table);
                if (!m_cell)
                    return;
                m_value = m_cell->value.load(turf::Consume);
                if (m_value != Value(ValueTraits::Redirect))
                    return; // Found an existing value
                // We've encountered a Redirect value. Help finish the migration.
                TURF_TRACE(ConcurrentMap_Linear, 1, "[Mutator] find was redirected", uptr(m_table), 0);
                m_table->jobCoordinator.participate();
                // Try again using the latest root.
            }
        }

        // Constructor: Insert cell
        Mutator(ConcurrentMap_Linear& map, Key key) : m_map(map), m_value(Value(ValueTraits::NullValue)) {
            TURF_TRACE(ConcurrentMap_Linear, 2, "[Mutator] insertOrFind constructor called", uptr(0), uptr(key));
            Hash hash = KeyTraits::hash(key);
            bool mustDouble = false;
            for (;;) {
                m_table = m_map.m_root.load(turf::Consume);
                switch (Details::insertOrFind(hash, m_table, m_cell)) { // Modifies m_cell
                case Details::InsertResult_InsertedNew: {
                    // We've inserted a new cell. Don't load m_cell->value.
                    return;
                }
                case Details::InsertResult_AlreadyFound: {
                    // The hash was already found in the table.
                    m_value = m_cell->value.load(turf::Consume);
                    if (m_value == Value(ValueTraits::Redirect)) {
                        // We've encountered a Redirect value.
                        TURF_TRACE(ConcurrentMap_Linear, 3, "[Mutator] insertOrFind was redirected", uptr(m_table), uptr(m_value));
                        break; // Help finish the migration.
                    }
                    return; // Found an existing value
                }
                case Details::InsertResult_Overflow: {
                    Details::beginTableMigration(m_map, m_table, mustDouble);
                    break;
                }
                }
                // A migration has been started (either by us, or another thread). Participate until it's complete.
                m_table->jobCoordinator.participate();
                // If we still overflow after this, avoid an infinite loop by forcing the next table to double.
                mustDouble = true;
                // Try again using the latest root.
            }
        }

    public:
        Value getValue() const {
            // Return previously loaded value. Don't load it again.
            return Value(m_value);
        }

        Value exchangeValue(Value desired) {
            TURF_ASSERT(desired != Value(ValueTraits::NullValue));
            TURF_ASSERT(desired != Value(ValueTraits::Redirect));
            TURF_ASSERT(m_cell); // Cell must have been found or inserted
            TURF_TRACE(ConcurrentMap_Linear, 4, "[Mutator::exchangeValue] called", uptr(m_table), uptr(m_value));
            bool mustDouble = false;
            for (;;) {
                Value oldValue = m_value;
                if (m_cell->value.compareExchangeStrong(m_value, desired, turf::ConsumeRelease)) {
                    // Exchange was successful. Return previous value.
                    TURF_TRACE(ConcurrentMap_Linear, 5, "[Mutator::exchangeValue] exchanged Value", uptr(m_value), uptr(desired));
                    Value result = m_value;
                    m_value = desired; // Leave the mutator in a valid state
                    return result;
                }
                // The CAS failed and m_value has been updated with the latest value.
                if (m_value != Value(ValueTraits::Redirect)) {
                    TURF_TRACE(ConcurrentMap_Linear, 6, "[Mutator::exchangeValue] detected race to write value", uptr(m_table),
                               uptr(m_value));
                    if (oldValue == Value(ValueTraits::NullValue) && m_value != Value(ValueTraits::NullValue)) {
                        TURF_TRACE(ConcurrentMap_Linear, 7, "[Mutator::exchangeValue] racing write inserted new value",
                                   uptr(m_table), uptr(m_value));
                    }
                    // There was a racing write (or erase) to this cell.
                    // Pretend we exchanged with ourselves, and just let the racing write win.
                    return desired;
                }
                // We've encountered a Redirect value. Help finish the migration.
                TURF_TRACE(ConcurrentMap_Linear, 8, "[Mutator::exchangeValue] was redirected", uptr(m_table), uptr(m_value));
                Hash hash = m_cell->hash.load(turf::Relaxed);
                for (;;) {
                    // Help complete the migration.
                    m_table->jobCoordinator.participate();
                    // Try again in the new table.
                    m_table = m_map.m_root.load(turf::Consume);
                    m_value = Value(ValueTraits::NullValue);
                    switch (Details::insertOrFind(hash, m_table, m_cell)) { // Modifies m_cell
                    case Details::InsertResult_AlreadyFound:
                        m_value = m_cell->value.load(turf::Consume);
                        if (m_value == Value(ValueTraits::Redirect)) {
                            TURF_TRACE(ConcurrentMap_Linear, 9, "[Mutator::exchangeValue] was re-redirected", uptr(m_table),
                                       uptr(m_value));
                            break;
                        }
                        goto breakOuter;
                    case Details::InsertResult_InsertedNew:
                        goto breakOuter;
                    case Details::InsertResult_Overflow:
                        TURF_TRACE(ConcurrentMap_Linear, 10, "[Mutator::exchangeValue] overflow after redirect", uptr(m_table), 0);
                        Details::beginTableMigration(m_map, m_table, mustDouble);
                        break;
                    }
                    // If we still overflow after this, avoid an infinite loop by forcing the next table to double.
                    mustDouble = true;
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
            TURF_TRACE(ConcurrentMap_Linear, 11, "[Mutator::eraseValue] called", uptr(m_table), m_cell - m_table->getCells());
            for (;;) {
                if (m_value == Value(ValueTraits::NullValue))
                    return Value(m_value);
                TURF_ASSERT(m_cell); // m_value is non-NullValue, therefore cell must have been found or inserted.
                if (m_cell->value.compareExchangeStrong(m_value, Value(ValueTraits::NullValue), turf::Consume)) {
                    // Exchange was successful and a non-NULL value was erased and returned by reference in m_value.
                    TURF_ASSERT(m_value != ValueTraits::NullValue); // Implied by the test at the start of the loop.
                    Value result = m_value;
                    m_value = Value(ValueTraits::NullValue); // Leave the mutator in a valid state
                    return result;
                }
                // The CAS failed and m_value has been updated with the latest value.
                TURF_TRACE(ConcurrentMap_Linear, 12, "[Mutator::eraseValue] detected race to write value", uptr(m_table),
                           m_cell - m_table->getCells());
                if (m_value != Value(ValueTraits::Redirect)) {
                    // There was a racing write (or erase) to this cell.
                    // Pretend we erased nothing, and just let the racing write win.
                    return Value(ValueTraits::NullValue);
                }
                // We've been redirected to a new table.
                TURF_TRACE(ConcurrentMap_Linear, 13, "[Mutator::eraseValue] was redirected", uptr(m_table),
                           m_cell - m_table->getCells());
                Hash hash = m_cell->hash.load(turf::Relaxed); // Re-fetch hash
                for (;;) {
                    // Help complete the migration.
                    m_table->jobCoordinator.participate();
                    // Try again in the new table.
                    m_table = m_map.m_root.load(turf::Consume);
                    m_cell = Details::find(hash, m_table);
                    if (!m_cell) {
                        m_value = Value(ValueTraits::NullValue);
                        return m_value;
                    }
                    m_value = m_cell->value.load(turf::Relaxed);
                    if (m_value != Value(ValueTraits::Redirect))
                        break;
                    TURF_TRACE(ConcurrentMap_Linear, 14, "[Mutator::eraseValue] was re-redirected", uptr(m_table),
                               m_cell - m_table->getCells());
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
        TURF_TRACE(ConcurrentMap_Linear, 15, "[get] called", uptr(this), uptr(hash));
        for (;;) {
            typename Details::Table* table = m_root.load(turf::Consume);
            typename Details::Cell* cell = Details::find(hash, table);
            if (!cell)
                return Value(ValueTraits::NullValue);
            Value value = cell->value.load(turf::Consume);
            if (value != Value(ValueTraits::Redirect))
                return value; // Found an existing value
            // We've been redirected to a new table. Help with the migration.
            TURF_TRACE(ConcurrentMap_Linear, 16, "[get] was redirected", uptr(table), uptr(cell));
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
    // To make it work with concurrent inserts, we'd need a way to block TableMigrations.
    class Iterator {
    private:
        typename Details::Table* m_table;
        ureg m_idx;
        Key m_hash;
        Value m_value;

    public:
        Iterator(ConcurrentMap_Linear& map) {
            // Since we've forbidden concurrent inserts (for now), nonatomic would suffice here, but let's plan ahead:
            m_table = map.m_root.load(turf::Consume);
            m_idx = -1;
            next();
        }

        void next() {
            TURF_ASSERT(m_table);
            TURF_ASSERT(isValid() || m_idx == -1); // Either the Iterator is already valid, or we've just started iterating.
            while (++m_idx <= m_table->sizeMask) {
                // Index still inside range of table.
                typename Details::Cell* cell = m_table->getCells() + m_idx;
                m_hash = cell->hash.load(turf::Relaxed);
                if (m_hash != KeyTraits::NullHash) {
                    // Cell has been reserved.
                    m_value = cell->value.load(turf::Relaxed);
                    TURF_ASSERT(m_value != Value(ValueTraits::Redirect));
                    if (m_value != Value(ValueTraits::NullValue))
                        return; // Yield this cell.
                }
            }
            // That's the end of the map.
            m_hash = KeyTraits::NullHash;
            m_value = Value(ValueTraits::NullValue);
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

#endif // JUNCTION_CONCURRENTMAP_LINEAR_H
