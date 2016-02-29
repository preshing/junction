/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_SINGLEMAP_LEAPFROG_H
#define JUNCTION_SINGLEMAP_LEAPFROG_H

#include <junction/Core.h>
#include <junction/MapTraits.h>
#include <turf/Util.h>
#include <turf/Heap.h>

namespace junction {

template <typename Key, typename Value, class KeyTraits = DefaultKeyTraits<Key>, class ValueTraits = DefaultValueTraits<Value>>
class SingleMap_Leapfrog {
private:
    typedef typename KeyTraits::Hash Hash;

    static const ureg InitialSize = 8;
    static const ureg LinearSearchLimit = 128;
    static const ureg CellsInUseSample = LinearSearchLimit;
    TURF_STATIC_ASSERT(LinearSearchLimit > 0 && LinearSearchLimit < 256);              // Must fit in CellGroup::links
    TURF_STATIC_ASSERT(CellsInUseSample > 0 && CellsInUseSample <= LinearSearchLimit); // Limit sample to failed search chain

    struct Cell {
        Hash hash;
        Value value;
        Cell(Hash hash, Value value) : hash(hash), value(value) {
        }
    };

    struct CellGroup {
        u8 deltas[8];
        Cell cells[4];
    };

    CellGroup* m_cellGroups;
    ureg m_sizeMask;

    static CellGroup* createTable(ureg size = InitialSize) {
        TURF_ASSERT(size >= 4 && turf::util::isPowerOf2(size));
        CellGroup* cellGroups = (CellGroup*) TURF_HEAP.alloc(sizeof(CellGroup) * (size >> 2));
        for (ureg i = 0; i < (size >> 2); i++) {
            CellGroup* group = cellGroups + i;
            ureg j;
            for (j = 0; j < 8; j++)
                group->deltas[j] = 0;
            for (j = 0; j < 4; j++)
                new (group->cells + j) Cell(KeyTraits::NullHash, Value(ValueTraits::NullValue));
        }
        return cellGroups;
    }

    static void destroyTable(CellGroup* cellGroups, ureg size) {
        TURF_ASSERT(size >= 4 && turf::util::isPowerOf2(size));
        for (ureg i = 0; i < (size >> 2); i++) {
            CellGroup* group = cellGroups + i;
            for (ureg j = 0; j < 4; j++)
                group->cells[i].~Cell();
        }
        TURF_HEAP.free(cellGroups);
    }

    enum InsertResult { InsertResult_AlreadyFound, InsertResult_InsertedNew, InsertResult_Overflow };
    InsertResult insertOrFind(Hash hash, Cell*& cell, ureg& overflowIdx) {
        TURF_ASSERT(hash != KeyTraits::NullHash);
        ureg idx = ureg(hash);
        // Check hashed cell first, though it may not even belong to the bucket.
        CellGroup* group = m_cellGroups + ((idx & m_sizeMask) >> 2);
        cell = group->cells + (idx & 3);
        if (cell->hash == hash)
            return InsertResult_AlreadyFound; // Key found in table.
        else if (cell->hash == KeyTraits::NullHash) {
            // Reserve the first cell.
            cell->hash = hash;
            return InsertResult_InsertedNew;
        }
        // Follow probe chain for our bucket.
        ureg maxIdx = idx + m_sizeMask;
        u8* prevLink = group->deltas + (idx & 3);
        u8 delta = *prevLink;
        while (delta) {
            idx += delta;
            group = m_cellGroups + ((idx & m_sizeMask) >> 2);
            cell = group->cells + (idx & 3);
            if (cell->hash == hash)
                return InsertResult_AlreadyFound; // Key found in table
            prevLink = group->deltas + (idx & 3) + 4;
            delta = *prevLink;
        }
        // Reached the end of the link chain for this bucket. Key does not exist in table.
        // Switch to linear probing to find a free cell.
        ureg prevLinkIdx = idx;
        TURF_ASSERT(sreg(maxIdx - idx) >= 0); // Nobody would have linked an idx that's out of range.
        ureg linearProbesRemaining = turf::util::min(maxIdx - idx, LinearSearchLimit);
        while (linearProbesRemaining-- > 0) {
            idx++;
            group = m_cellGroups + ((idx & m_sizeMask) >> 2);
            cell = group->cells + (idx & 3);
            if (cell->hash == KeyTraits::NullHash) {
                // It's an empty cell. Reserve it.
                cell->hash = hash;
                // Link it to previous cell in the same bucket.
                *prevLink = idx - prevLinkIdx;
                return InsertResult_InsertedNew;
            }
            // In a single-threaded map, it's impossible for a matching hash to appear outside the probe chain.
            TURF_ASSERT(cell->hash != hash);
            // Continue linear search...
        }
        // Table is too full to insert.
        overflowIdx = idx + 1;
        return InsertResult_Overflow;
    }

    bool tryMigrateToNewTableWithSize(ureg desiredSize) {
        CellGroup* srcCellGroups = m_cellGroups;
        ureg srcSize = m_sizeMask + 1;
        m_cellGroups = createTable(desiredSize);
        m_sizeMask = desiredSize - 1;
        for (ureg srcIdx = 0; srcIdx < srcSize; srcIdx++) {
            CellGroup* srcGroup = srcCellGroups + (srcIdx >> 2);
            Cell* srcCell = srcGroup->cells + (srcIdx & 3);
            if (srcCell->value != Value(ValueTraits::NullValue)) {
                Cell* dstCell;
                ureg overflowIdx;                
                InsertResult result = insertOrFind(srcCell->hash, dstCell, overflowIdx);
                TURF_ASSERT(result != InsertResult_AlreadyFound);
                if (result == InsertResult_Overflow) {
                    // Migration failed; destination table too small
                    destroyTable(m_cellGroups, m_sizeMask + 1);
                    m_cellGroups = srcCellGroups;
                    m_sizeMask = srcSize - 1;
                    return false;
                }
                dstCell->value = srcCell->value;
            }
        }
        destroyTable(srcCellGroups, srcSize);
        return true;
    }

    void migrateToNewTable(ureg overflowIdx) {
        // Estimate number of cells in use based on a small sample.
        ureg idx = overflowIdx - CellsInUseSample;
        ureg inUseCells = 0;
        for (ureg linearProbesRemaining = CellsInUseSample; linearProbesRemaining > 0; linearProbesRemaining--) {
            CellGroup* group = m_cellGroups + ((idx & m_sizeMask) >> 2);
            Cell* cell = group->cells + (idx & 3);
            if (cell->value != Value(ValueTraits::NullValue))
                inUseCells++;
            idx++;
        }
        float inUseRatio = float(inUseCells) / CellsInUseSample;
        float estimatedInUse = (m_sizeMask + 1) * inUseRatio;
#if JUNCTION_LEAPFROG_FORCE_MIGRATION_OVERFLOWS
        // Periodically underestimate the number of cells in use.
        // This exercises the code that handles overflow during migration.
        static ureg counter = 1;
        if ((++counter & 3) == 0) {
            estimatedInUse /= 4;
        }
#endif
        ureg nextTableSize = turf::util::max(InitialSize, turf::util::roundUpPowerOf2(ureg(estimatedInUse * 2)));
        for (;;) {
            if (tryMigrateToNewTableWithSize(nextTableSize))
                break; // Success
            // Failed; try a larger table
            nextTableSize *= 2;
        }
    }

public:
    SingleMap_Leapfrog(ureg initialSize = 8) : m_cellGroups(createTable(initialSize)), m_sizeMask(initialSize - 1) {
    }

    ~SingleMap_Leapfrog() {
        destroyTable(m_cellGroups, m_sizeMask + 1);
    }

    class Mutator {
    private:
        friend class SingleMap_Leapfrog;
        SingleMap_Leapfrog& m_map;
        Cell* m_cell;

        // Constructor: Find without insert
        Mutator(SingleMap_Leapfrog& map, Key key, bool) : m_map(map) {
            Hash hash = KeyTraits::hash(key);
            TURF_ASSERT(hash != KeyTraits::NullHash);
            // Optimistically check hashed cell even though it might belong to another bucket
            ureg idx = ureg(hash);
            CellGroup* group = map.m_cellGroups + ((idx & map.m_sizeMask) >> 2);
            m_cell = group->cells + (idx & 3);
            if (m_cell->hash == hash)
                return; // Key found in table.
            // Follow probe chain for our bucket.
            u8 delta = group->deltas[idx & 3];
            while (delta) {
                idx += delta;
                group = map.m_cellGroups + ((idx & map.m_sizeMask) >> 2);
                m_cell = group->cells + (idx & 3);
                if (m_cell->hash == hash)
                    return; // Key found in table
                delta = group->deltas[(idx & 3) + 4];
            }
            // End of probe chain, not found
            m_cell = NULL;
            return;
        }

        // Constructor: Find with insert
        Mutator(SingleMap_Leapfrog& map, Key key) : m_map(map) {
            Hash hash = KeyTraits::hash(key);
            for (;;) {
                ureg overflowIdx;
                if (map.insertOrFind(hash, m_cell, overflowIdx) != InsertResult_Overflow)
                    return;
                // Insert overflow. Migrate and try again.
                // On the first iteration of this loop, deleted cells will be purged.
                // The second iteration (if any) will always double in size.
                // On the third iteration (if any), the insert will succeed.
                map.migrateToNewTable(overflowIdx);
            }
        }

    public:
        bool isValid() const {
            return !!m_cell;
        }

        Value getValue() const {
            TURF_ASSERT(m_cell);
            return m_cell->value;
        }

        Value exchangeValue(Value desired) {
            TURF_ASSERT(m_cell);
            TURF_ASSERT(desired != NULL); // Use eraseValue()
            Value oldValue = m_cell->value;
            m_cell->value = desired;
            return oldValue;
        }

        Value erase() {
            TURF_ASSERT(m_cell);
            // Since this map is single-threaded, we could conceivably shuffle existing cells around to safely fill
            // gaps, much like SingleMap_Linear currently does.
            // Instead, we'll just leave them as deleted entries and let them get purged on the next migration.
            Value oldValue = m_cell->value;
            m_cell->value = Value(ValueTraits::NullValue);
            return oldValue;
        }
    };

    Mutator insertOrFindKey(const Key& key) {
        return Mutator(*this, key);
    }

    Value get(const Key& key) {
        Mutator iter(*this, key, false);
        return iter.isValid() ? iter.getValue() : NULL;
    }

    Value set(const Key& key, Value desired) {
        Mutator iter(*this, key);
        return iter.exchangeValue(desired);
    }

    Value erase(const Key& key) {
        Mutator iter(*this, key, false);
        if (iter.isValid())
            return iter.erase();
        return NULL;
    }
};

} // namespace junction

#endif // JUNCTION_SINGLEMAP_LEAPFROG_H
