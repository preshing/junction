/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_EXTRA_IMPL_MAPADAPTER_TERVEL_H
#define JUNCTION_EXTRA_IMPL_MAPADAPTER_TERVEL_H

#include <junction/Core.h>

#if !JUNCTION_WITH_TERVEL
#error "You must configure with JUNCTION_WITH_TERVEL=1!"
#endif

#include <tervel/util/tervel.h>
#include <tervel/containers/wf/hash-map/wf_hash_map.h>

namespace junction {
namespace extra {

class MapAdapter {
public:
    static TURF_CONSTEXPR const char* getMapName() { return "Tervel HashMap"; }

    tervel::Tervel m_tervel;

    MapAdapter(ureg numThreads) : m_tervel(numThreads) {
    }

    class ThreadContext {
    public:
        MapAdapter& m_adapter;
        tervel::ThreadContext* m_context;

        ThreadContext(MapAdapter& adapter, ureg) : m_adapter(adapter), m_context(NULL) {
        }

        void registerThread() {
            m_context = new tervel::ThreadContext(&m_adapter.m_tervel);
        }

        void unregisterThread() {
            delete m_context;
        }

        void update() {
        }
    };

    class Map {
    private:
        tervel::containers::wf::HashMap<u64, u64> m_map;

    public:
        Map(ureg capacity) : m_map(capacity, 3) {
        }

        void assign(u32 key, void* value) {
            m_map.insert(key, (u64) value);
        }

        void* get(u32 key) {
            typename tervel::containers::wf::HashMap<u64, u64>::ValueAccessor va;
            if (m_map.at(key, va))
                return (void*) *va.value();
            else
                return NULL;
        }

        void erase(u32 key) {
            m_map.remove(key);
        }
    };

    static ureg getInitialCapacity(ureg maxPopulation) {
        return maxPopulation / 4;
    }
};

} // namespace extra
} // namespace junction

#endif // JUNCTION_EXTRA_IMPL_MAPADAPTER_TERVEL_H
