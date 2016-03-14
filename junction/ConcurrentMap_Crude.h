/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_CONCURRENTMAP_CRUDE_H
#define JUNCTION_CONCURRENTMAP_CRUDE_H

#include <junction/Core.h>
#include <junction/MapTraits.h>

namespace junction {

template <typename K, typename V, class KT = DefaultKeyTraits<K>, class VT = DefaultValueTraits<V> >
class ConcurrentMap_Crude {
public:
    typedef K Key;
    typedef V Value;
    typedef KT KeyTraits;
    typedef VT ValueTraits;
    
    static const ureg DefaultCapacity = 256;

private:
    struct Cell {
        turf::Atomic<Key> key;
        turf::Atomic<Value> value;
    };

    Cell* m_cells;
    ureg m_sizeMask;

public:
    ConcurrentMap_Crude(ureg capacity = DefaultCapacity) {
        TURF_ASSERT(turf::util::isPowerOf2(capacity));
        m_sizeMask = capacity - 1;
        m_cells = new Cell[capacity];
        clear();
    }

    ~ConcurrentMap_Crude() {
        delete[] m_cells;
    }

    void assign(Key key, Value value) {
        TURF_ASSERT(key != KeyTraits::NullKey);
        TURF_ASSERT(value != Value(ValueTraits::NullValue));

        for (ureg idx = KeyTraits::hash(key);; idx++) {
            idx &= m_sizeMask;
            Cell* cell = m_cells + idx;

            // Load the key that was there.
            Key probedKey = cell->key.load(turf::Relaxed);
            if (probedKey != key) {
                // The cell was either free, or contains another key.
                if (probedKey != KeyTraits::NullKey)
                    continue; // Usually, it contains another key. Keep probing.

                // The cell was free. Now let's try to take it using a CAS.
                Key prevKey = cell->key.compareExchange(probedKey, key, turf::Relaxed);
                if ((prevKey != KeyTraits::NullKey) && (prevKey != key))
                    continue; // Another thread just stole it from underneath us.

                // Either we just added the key, or another thread did.
            }

            // Store the value in this cell.
            cell->value.store(value, turf::Relaxed);
            return;
        }
    }

    Value get(Key key) {
        TURF_ASSERT(key != KeyTraits::NullKey);

        for (ureg idx = KeyTraits::hash(key);; idx++) {
            idx &= m_sizeMask;
            Cell* cell = m_cells + idx;

            Key probedKey = cell->key.load(turf::Relaxed);
            if (probedKey == key)
                return cell->value.load(turf::Relaxed);
            if (probedKey == KeyTraits::NullKey)
                return Value(ValueTraits::NullValue);
        }
    }

    void clear() {
        // Must be called when there are no concurrent readers or writers
        for (ureg idx = 0; idx <= m_sizeMask; idx++) {
            Cell* cell = m_cells + idx;
            cell->key.storeNonatomic(KeyTraits::NullKey);
            cell->value.storeNonatomic(Value(ValueTraits::NullValue));
        }
    }
};

} // namespace junction

#endif // JUNCTION_CONCURRENTMAP_CRUDE_H
