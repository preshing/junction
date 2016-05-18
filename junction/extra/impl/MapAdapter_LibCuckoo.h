/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_EXTRA_IMPL_MAPADAPTER_LIBCUCKOO_H
#define JUNCTION_EXTRA_IMPL_MAPADAPTER_LIBCUCKOO_H

#include <junction/Core.h>

#if !JUNCTION_WITH_LIBCUCKOO
#error "You must configure with JUNCTION_WITH_LIBCUCKOO=1!"
#endif

#include <libcuckoo/cuckoohash_map.hh>

namespace junction {
namespace extra {

class MapAdapter {
public:
    static TURF_CONSTEXPR const char* getMapName() { return "libcuckoo cuckoohash_map"; }

    MapAdapter(ureg) {
    }

    class ThreadContext {
    public:
        ThreadContext(MapAdapter&, ureg) {
        }

        void registerThread() {
        }

        void unregisterThread() {
        }

        void update() {
        }
    };

    class Map {
    private:
        cuckoohash_map<u32, void*> m_map;

    public:
        Map(ureg capacity) : m_map(capacity) {
        }

        void assign(u32 key, void* value) {
            m_map.insert(key, value);
        }

        void* get(u32 key) {
            void* result;
            return m_map.find(key, result) ? result : NULL;
        }

        void erase(u32 key) {
            m_map.erase(key);
        }
    };

    static ureg getInitialCapacity(ureg maxPopulation) {
        return maxPopulation / 4;
    }
};

} // namespace extra
} // namespace junction

#endif // JUNCTION_EXTRA_IMPL_MAPADAPTER_LIBCUCKOO_H
