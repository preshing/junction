/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_SINGLEMAP_LINEAR_H
#define JUNCTION_SINGLEMAP_LINEAR_H

#include <junction/Core.h>
#include <junction/MapTraits.h>
#include <turf/Util.h>
#include <turf/Heap.h>

namespace junction {

template <typename Key, typename Value, class KeyTraits = DefaultKeyTraits<Key>, class ValueTraits = DefaultValueTraits<Value>>
class SingleMap_Linear {
private:
    typedef typename KeyTraits::Hash Hash;

    struct Cell {
        Hash hash;
        Value value;
        Cell(Hash hash, Value value) : hash(hash), value(value) {
        }
    };

    Cell* m_cells;
    ureg m_sizeMask;
    ureg m_population;

    static Cell* createTable(ureg size) {
        TURF_ASSERT(turf::util::isPowerOf2(size));
        Cell* cells = (Cell*) TURF_HEAP.alloc(sizeof(Cell) * size);
        for (ureg i = 0; i < size; i++)
            new (cells + i) Cell(KeyTraits::NullHash, Value(ValueTraits::NullValue));
        return cells;
    }

    static void destroyTable(Cell* cells, ureg size) {
        TURF_ASSERT(turf::util::isPowerOf2(size));
        for (ureg i = 0; i < size; i++)
            cells[i].~Cell();
        TURF_HEAP.free(cells);
    }

    static bool isOverpopulated(ureg population, ureg sizeMask) {
        return (population * 4) >= (sizeMask * 3);
    }

    void migrateToNewTable(ureg desiredSize) {
        Cell* srcCells = m_cells;
        ureg srcSize = m_sizeMask + 1;
        m_cells = createTable(desiredSize);
        m_sizeMask = desiredSize - 1;
        for (ureg srcIdx = 0; srcIdx < srcSize; srcIdx++) {
            Cell* srcCell = srcCells + srcIdx;
            if (srcCell->hash != KeyTraits::NullHash) {
                for (ureg dstIdx = srcCell->hash;; dstIdx++) {
                    dstIdx &= m_sizeMask;
                    if (m_cells[dstIdx].hash == KeyTraits::NullHash) {
                        m_cells[dstIdx] = *srcCell;
                        break;
                    }
                }
            }
        }
        destroyTable(srcCells, srcSize);
    }

public:
    SingleMap_Linear(ureg initialSize = 8) : m_cells(createTable(initialSize)), m_sizeMask(initialSize - 1), m_population(0) {
    }

    ~SingleMap_Linear() {
        destroyTable(m_cells, m_sizeMask + 1);
    }

    class Mutator {
    private:
        friend class SingleMap_Linear;
        SingleMap_Linear& m_map;
        Cell* m_cell;

        // Constructor: Find without insert
        Mutator(SingleMap_Linear& map, Key key, bool) : m_map(map) {
            Hash hash = KeyTraits::hash(key);
            TURF_ASSERT(hash != KeyTraits::NullHash);
            for (ureg idx = hash;; idx++) {
                idx &= map.m_sizeMask;
                m_cell = map.m_cells + idx;
                if (m_cell->hash == hash)
                    return; // Key found in table.
                if (m_cell->hash != KeyTraits::NullHash)
                    continue;  // Slot is taken by another key. Try next slot.
                m_cell = NULL; // Insert not allowed & key not found.
                return;
            }
        }

        // Constructor: Find with insert
        Mutator(SingleMap_Linear& map, Key key) : m_map(map) {
            Hash hash = KeyTraits::hash(key);
            TURF_ASSERT(hash != KeyTraits::NullHash);
            for (;;) {
                for (ureg idx = hash;; idx++) {
                    idx &= map.m_sizeMask;
                    m_cell = map.m_cells + idx;
                    if (m_cell->hash == hash)
                        return; // Key found in table.
                    if (m_cell->hash != KeyTraits::NullHash)
                        continue; // Slot is taken by another key. Try next slot.
                    // Insert is allowed. Reserve this cell.
                    if (isOverpopulated(map.m_population, map.m_sizeMask)) {
                        map.migrateToNewTable((map.m_sizeMask + 1) * 2);
                        break; // Retry in new table.
                    }
                    map.m_population++;
                    m_cell->hash = hash;
                    TURF_ASSERT(m_cell->value == Value(ValueTraits::NullValue));
                    return;
                }
            }
        }

        ~Mutator() {
            TURF_ASSERT(!m_cell || m_cell->value != NULL); // In SingleMap_Linear, there are no deleted cells.
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
            TURF_ASSERT(m_cell->value != Value(ValueTraits::NullValue)); // SingleMap_Linear never contains "deleted" cells.
            Value oldValue = m_cell->value;
            // Remove this cell by shuffling neighboring cells so there are no gaps in anyone's probe chain
            ureg cellIdx = m_cell - m_map.m_cells;
            for (ureg neighborIdx = cellIdx + 1;; neighborIdx++) {
                neighborIdx &= m_map.m_sizeMask;
                Cell* neighbor = m_map.m_cells + neighborIdx;
                if (neighbor->hash == KeyTraits::NullHash) {
                    // Go ahead and clear this cell. It won't break anyone else's probe chain.
                    m_cell->hash = KeyTraits::NullHash;
                    m_cell->value = Value(ValueTraits::NullValue);
                    m_cell = NULL;
                    m_map.m_population--;
                    return oldValue;
                }
                ureg idealIdx = neighbor->hash & m_map.m_sizeMask;
                if (((cellIdx - idealIdx) & m_map.m_sizeMask) < ((neighborIdx - idealIdx) & m_map.m_sizeMask)) {
                    // Swap with neighbor, then make neighbor the new cell to remove.
                    *m_cell = *neighbor;
                    m_cell = neighbor;
                    cellIdx = neighborIdx;
                }
            }
        }
    };

    Mutator insertOrFindKey(const Key& key) {
        return Mutator(*this, key);
    }

    Value get(const Key& key) {
        Mutator iter(*this, key, false);
        return iter.isValid() ? iter.getValue() : NULL;
    }

    Value assign(const Key& key, Value desired) {
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

#endif // JUNCTION_SINGLEMAP_LINEAR_H
