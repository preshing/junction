/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_DETAILS_LINEAR_H
#define JUNCTION_DETAILS_LINEAR_H

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

TURF_TRACE_DECLARE(Linear, 22)

template<class Map>
struct Linear {
    typedef typename Map::Hash Hash;
    typedef typename Map::Value Value;
    typedef typename Map::KeyTraits KeyTraits;
    typedef typename Map::ValueTraits ValueTraits;

    static const ureg InitialSize = 8;
    static const ureg TableMigrationUnitSize = 32;
    static const ureg LinearSearchLimit = 128;
    static const ureg CellsInUseSample = LinearSearchLimit;
    TURF_STATIC_ASSERT(LinearSearchLimit > 0 && LinearSearchLimit < 256); // Must fit in CellGroup::links
    TURF_STATIC_ASSERT(CellsInUseSample > 0 && CellsInUseSample <= LinearSearchLimit); // Limit sample to failed search chain

    struct Cell {
        turf::Atomic<Hash> hash;
        turf::Atomic<Value> value;
    };

    struct Table {
        const ureg sizeMask;                    // a power of two minus one
        const ureg limitNumValues;
        turf::Atomic<sreg> cellsRemaining;
        turf::Atomic<sreg> valuesRemaining;
        turf::Mutex mutex;                      // to DCLI the TableMigration (stored in the jobCoordinator)
        SimpleJobCoordinator jobCoordinator;    // makes all blocked threads participate in the migration

        Table(ureg sizeMask, ureg limitNumValues) : sizeMask(sizeMask), limitNumValues(limitNumValues),
            cellsRemaining(limitNumValues), valuesRemaining(limitNumValues) {
        }

        static Table* create(ureg tableSize, ureg limitNumValues) {
            TURF_ASSERT(turf::util::isPowerOf2(tableSize));
            Table* table = (Table*) TURF_HEAP.alloc(sizeof(Table) + sizeof(Cell) * tableSize);
            new(table) Table(tableSize - 1, limitNumValues);
            for (ureg j = 0; j < tableSize; j++) {
                table->getCells()[j].hash.storeNonatomic(KeyTraits::NullHash);
                table->getCells()[j].value.storeNonatomic(Value(ValueTraits::NullValue));
            }
            return table;
        }

        void destroy() {
            this->Table::~Table();
            TURF_HEAP.free(this);
        }

        Cell* getCells() const {
            return (Cell*) (this + 1);
        }

        ureg getNumMigrationUnits() const {
            return sizeMask / TableMigrationUnitSize + 1;
        }
    };

    class TableMigration : public SimpleJobCoordinator::Job {
    public:
        Map& m_map;
        Table* m_source;
        turf::Atomic<ureg> m_sourceIndex;
        Table* m_destination;
        turf::Atomic<ureg> m_workerStatus;          // number of workers + end flag
        turf::Atomic<sreg> m_unitsRemaining;

        TableMigration(Map& map) : m_map(map), m_sourceIndex(0), m_workerStatus(0), m_unitsRemaining(0) {
            // Caller is responsible for filling in source & destination
        }

        virtual ~TableMigration() TURF_OVERRIDE {
            // Destroy source table.
            m_source->destroy();
        }

        void destroy() {
            delete this;
        }

        bool migrateRange(ureg startIdx);
        virtual void run() TURF_OVERRIDE;
    };

    static Cell* find(Hash hash, Table* table) {
        TURF_TRACE(Linear, 0, "[find] called", uptr(table), hash);
        TURF_ASSERT(table);
        TURF_ASSERT(hash != KeyTraits::NullHash);
        ureg sizeMask = table->sizeMask;
        for (ureg idx = hash;; idx++) {
            idx &= sizeMask;
            Cell* cell = table->getCells() + idx;
            // Load the hash that was there.
            uptr probeHash = cell->hash.load(turf::Relaxed);
            if (probeHash == hash) {
                TURF_TRACE(Linear, 1, "[find] found existing cell", uptr(table), idx);
                return cell;
            } else if (probeHash == KeyTraits::NullHash) {
                return NULL;
            }
        }
    }

    // FIXME: Possible optimization: Dedicated insert for migration? It wouldn't check for InsertResult_AlreadyFound.
    enum InsertResult {
        InsertResult_AlreadyFound,
        InsertResult_InsertedNew,
        InsertResult_Overflow
    };
    static InsertResult insert(Hash hash, Table* table, Cell*& cell) {
        TURF_TRACE(Linear, 2, "[insert] called", uptr(table), hash);
        TURF_ASSERT(table);
        TURF_ASSERT(hash != KeyTraits::NullHash);
        ureg sizeMask = table->sizeMask;

        for (ureg idx = hash;; idx++) {
            idx &= sizeMask;
            cell = table->getCells() + idx;
            // Load the existing hash.
            uptr probeHash = cell->hash.load(turf::Relaxed);
            if (probeHash == hash) {
                TURF_TRACE(Linear, 3, "[insert] found existing cell", uptr(table), idx);
                return InsertResult_AlreadyFound;       // Key found in table. Return the existing cell.
            }
            if (probeHash == KeyTraits::NullHash) {
                // It's an empty cell. Try to reserve it.
                // But first, decrement cellsRemaining to ensure we have permission to create new getCells().
                s32 prevCellsRemaining = table->cellsRemaining.fetchSub(1, turf::Relaxed);
                if (prevCellsRemaining <= 0) {
                    // Table is overpopulated.
                    TURF_TRACE(Linear, 4, "[insert] ran out of cellsRemaining", prevCellsRemaining, 0);
                    table->cellsRemaining.fetchAdd(1, turf::Relaxed);    // Undo cellsRemaining decrement
                    return InsertResult_Overflow;
                }
                // Try to reserve this cell.
                uptr prevHash = cell->hash.compareExchange(KeyTraits::NullHash, hash, turf::Relaxed);
                if (prevHash == KeyTraits::NullHash) {
                    // Success. We reserved a new cell.
                    TURF_TRACE(Linear, 5, "[insert] reserved cell", prevCellsRemaining, idx);
                    return InsertResult_InsertedNew;
                }
                // There was a race and another thread reserved that cell from under us.
                TURF_TRACE(Linear, 6, "[insert] detected race to reserve cell", ureg(hash), idx);
                table->cellsRemaining.fetchAdd(1, turf::Relaxed);        // Undo cellsRemaining decrement
                if (prevHash == hash) {
                    TURF_TRACE(Linear, 7, "[insert] race reserved same hash", ureg(hash), idx);
                    return InsertResult_AlreadyFound;       // They inserted the same key. Return the existing cell.
                }
            }
            // Try again in the next cell.
        }
    }

    static void beginTableMigration(Map& map, Table* table) {
        // Create new migration by DCLI.
        TURF_TRACE(Linear, 8, "[beginTableMigration] called", 0, 0);
        SimpleJobCoordinator::Job* job = table->jobCoordinator.loadConsume();
        if (job) {
            TURF_TRACE(Linear, 9, "[beginTableMigration] new migration already exists", 0, 0);
        } else {
            turf::LockGuard<turf::Mutex> guard(table->mutex);
            job = table->jobCoordinator.loadConsume();  // Non-atomic would be sufficient, but that's OK.
            if (job) {
                TURF_TRACE(Linear, 10, "[beginTableMigration] new migration already exists (double-checked)", 0, 0);
            } else {
                // Determine new migration size and cap the number of values that can be added concurrent to the migration.
                sreg oldValuesLimit = table->limitNumValues;
                sreg oldValuesRemaining = table->valuesRemaining.load(turf::Relaxed);
                sreg oldValuesInUse = oldValuesLimit - oldValuesRemaining;
            calculateNextTableSize:
                sreg nextTableSize = turf::util::roundUpPowerOf2(oldValuesInUse * 2);
                sreg nextLimitNumValues = nextTableSize * 3 / 4;
                if (nextLimitNumValues < oldValuesLimit) {
                    // Set the new limitNumValues on the *current* table.
                    // This prevents other threads, while the migration is in progress, from concurrently
                    // re-inserting more values than the new table can hold.
                    // To set the new limitNumValues on the current table in an atomic fashion,
                    // we update its valuesRemaining via CAS loop:
                    for(;;) {
                        // We must recalculate desiredValuesRemaining on each iteration of the CAS loop
                        oldValuesInUse = oldValuesLimit - oldValuesRemaining;
                        sreg desiredValuesRemaining = nextLimitNumValues - oldValuesInUse;
                        if (desiredValuesRemaining < 0) {
                            TURF_TRACE(Linear, 11, "[table] restarting valuesRemaining CAS loop", nextLimitNumValues, desiredValuesRemaining);
                            // Must recalculate nextTableSize. Goto, baby!
                            goto calculateNextTableSize;
                        }
                        if (table->valuesRemaining.compareExchangeWeak(oldValuesRemaining, desiredValuesRemaining, turf::Relaxed, turf::Relaxed))
                            break; // Success!
                        // CAS failed because table->valuesRemaining was modified by another thread.
                        // An updated value has been reloaded into oldValuesRemaining (modified by reference).
                        // Recalculate desiredValuesRemaining to account for the updated value, and try again.
                        TURF_TRACE(Linear, 12, "[table] valuesRemaining CAS failed", oldValuesRemaining, desiredValuesRemaining);
                    }
                }
                // Now we are assured that the new table will not become overpopulated during the migration process.
                // Create new migration.
                TableMigration* migration = new TableMigration(map);
                migration->m_source = table;
                migration->m_destination = Table::create(nextTableSize, nextLimitNumValues);
                migration->m_unitsRemaining.storeNonatomic(table->getNumMigrationUnits());
                // Publish the new migration.
                table->jobCoordinator.storeRelease(migration);
            }
        }
    }
}; // Linear

template<class Map>
bool Linear<Map>::TableMigration::migrateRange(ureg startIdx) {
    ureg srcSizeMask = m_source->sizeMask;
    ureg endIdx = turf::util::min(startIdx + TableMigrationUnitSize, srcSizeMask + 1);
    sreg valuesMigrated = 0;
    // Iterate over source range.
    for (ureg srcIdx = startIdx; srcIdx < endIdx; srcIdx++) {
        Cell* srcCell = m_source->getCells() + (srcIdx & srcSizeMask);
        Hash srcHash;
        Value srcValue;
        // Fetch the srcHash and srcValue.
        for (;;) {
            srcHash = srcCell->hash.load(turf::Relaxed);
            if (srcHash == KeyTraits::NullHash) {
                // An unused cell. Try to put a Redirect marker in its value.
                srcValue = srcCell->value.compareExchange(Value(ValueTraits::NullValue), Value(ValueTraits::Redirect), turf::Relaxed);
                if (srcValue == Value(ValueTraits::Redirect)) {
                    // srcValue is already marked Redirect due to previous incomplete migration.
                    TURF_TRACE(Linear, 13, "[migrateRange] empty cell already redirected", uptr(m_source), srcIdx);
                    break;
                }
                if (srcValue == Value(ValueTraits::NullValue))
                    break;  // Redirect has been placed. Break inner loop, continue outer loop.
                TURF_TRACE(Linear, 14, "[migrateRange] race to insert key", uptr(m_source), srcIdx);
                // Otherwise, somebody just claimed the cell. Read srcHash again...
            } else {
                // Check for deleted/uninitialized value.
                srcValue = srcCell->value.load(turf::Relaxed);
                if (srcValue == Value(ValueTraits::NullValue)) {
                    // Try to put a Redirect marker.
                    if (srcCell->value.compareExchangeStrong(srcValue, Value(ValueTraits::Redirect), turf::Relaxed))
                        break;  // Redirect has been placed. Break inner loop, continue outer loop.
                    TURF_TRACE(Linear, 15, "[migrateRange] race to insert value", uptr(m_source), srcIdx);
                }
                
                // We've got a key/value pair to migrate.
                // Reserve a destination cell in the destination.
                TURF_ASSERT(srcHash != KeyTraits::NullHash);
                TURF_ASSERT(srcValue != Value(ValueTraits::NullValue));
                TURF_ASSERT(srcValue != Value(ValueTraits::Redirect));   // Incomplete/concurrent migrations are impossible.
                Cell* dstCell;
                InsertResult result = insert(srcHash, m_destination, dstCell);
                // During migration, a hash can only exist in one place among all the source tables,
                // and it is only migrated by one thread. Therefore, the hash will never already exist
                // in the destination table:
                TURF_ASSERT(result != InsertResult_AlreadyFound);
                TURF_ASSERT(result != InsertResult_Overflow);
                // Migrate the old value to the new cell.
                for (;;) {
                    // Copy srcValue to the destination.
                    dstCell->value.store(srcValue, turf::Relaxed);
                    // Try to place a Redirect marker in srcValue.
                    Value doubleCheckedSrcValue = srcCell->value.compareExchange(srcValue, Value(ValueTraits::Redirect), turf::Relaxed);
                    TURF_ASSERT(doubleCheckedSrcValue != Value(ValueTraits::Redirect)); // Only one thread can redirect a cell at a time.
                    if (doubleCheckedSrcValue == srcValue) {
                        // No racing writes to the src. We've successfully placed the Redirect marker.
                        // srcValue was non-NULL when we decided to migrate it, but it may have changed to NULL
                        // by a late-arriving erase.
                        if (srcValue == Value(ValueTraits::NullValue))
                            TURF_TRACE(Linear, 16, "[migrateRange] racing update was erase", uptr(m_source), srcIdx);
                        else
                            valuesMigrated++;
                        break;
                    }
                    // There was a late-arriving write (or erase) to the src. Migrate the new value and try again.
                    TURF_TRACE(Linear, 17, "[migrateRange] race to update migrated value", uptr(m_source), srcIdx);
                    srcValue = doubleCheckedSrcValue;
                }
                // Cell successfully migrated. Proceed to next source cell.
                break;
            }
        }
    }
    sreg prevValuesRemaining = m_destination->valuesRemaining.fetchSub(valuesMigrated, turf::Relaxed);
    TURF_ASSERT(valuesMigrated <= prevValuesRemaining);
    TURF_UNUSED(prevValuesRemaining);
    // Range has been migrated successfully.
    return true;
}

template <class Map>
void Linear<Map>::TableMigration::run() {
    // Conditionally increment the shared # of workers.
    ureg probeStatus = m_workerStatus.load(turf::Relaxed);
    do {
        if (probeStatus & 1) {
            // End flag is already set, so do nothing.
            TURF_TRACE(Linear, 18, "[TableMigration::run] already ended", uptr(this), 0);
            return;
        }
    } while (!m_workerStatus.compareExchangeWeak(probeStatus, probeStatus + 2, turf::Relaxed, turf::Relaxed));
    // # of workers has been incremented, and the end flag is clear.
    TURF_ASSERT((probeStatus & 1) == 0);

    // Loop over all migration units in the source table.
    for (;;) {
        if (m_workerStatus.load(turf::Relaxed) & 1) {
            TURF_TRACE(Linear, 19, "[TableMigration::run] detected end flag set", uptr(this), 0);
            goto endMigration;
        }
        ureg startIdx = m_sourceIndex.fetchAdd(TableMigrationUnitSize, turf::Relaxed);
        if (startIdx >= m_source->sizeMask + 1)
            break;   // No more migration units.
        migrateRange(startIdx);
        sreg prevRemaining = m_unitsRemaining.fetchSub(1, turf::Relaxed);
        TURF_ASSERT(prevRemaining > 0);
        if (prevRemaining == 1) {
            // That was the last chunk to migrate.
            m_workerStatus.fetchOr(1, turf::Relaxed);
            goto endMigration;
        }
    }
    TURF_TRACE(Linear, 20, "[TableMigration::run] out of migration units", uptr(this), 0);

endMigration:
    // Decrement the shared # of workers.
    probeStatus = m_workerStatus.fetchSub(2, turf::AcquireRelease);    // AcquireRelease makes all previous writes visible to the last worker thread.
    if (probeStatus >= 4) {
        // There are other workers remaining. Return here so that only the very last worker will proceed.
        TURF_TRACE(Linear, 21, "[TableMigration::run] not the last worker", uptr(this), uptr(probeStatus));
        return;
    }

    // We're the very last worker thread.
    // Publish the new subtree.
    TURF_ASSERT(probeStatus == 3);
    m_map.publishTableMigration(this);
    // End the jobCoodinator.
    m_source->jobCoordinator.end();

    // We're done with this TableMigration. Queue it for GC.
    DefaultQSBR.enqueue(&TableMigration::destroy, this);
}

} // namespace details
} // namespace junction

#endif // JUNCTION_DETAILS_LINEAR_H
