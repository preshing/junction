/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_EXTRA_IMPL_MAPADAPTER_FOLLY_H
#define JUNCTION_EXTRA_IMPL_MAPADAPTER_FOLLY_H

#include <junction/Core.h>

#if !JUNCTION_WITH_FOLLY
#error "You must configure with JUNCTION_WITH_FOLLY=1!"
#endif

#include <folly/AtomicHashMap.h>

namespace junction {
namespace extra {

class MapAdapter {
public:
    static TURF_CONSTEXPR const char* getMapName() { return "Folly AtomicHashMap"; }

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
        folly::AtomicHashMap<u32, void*> m_map;

    public:
        Map(ureg capacity) : m_map(capacity) {
        }

        void assign(u32 key, void* value) {
            m_map.insert(std::make_pair(key, value));
        }

        void* get(u32 key) {
            auto ret = m_map.find(key);
            return ret != m_map.end() ? ret->second : NULL;
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

#endif // JUNCTION_EXTRA_IMPL_MAPADAPTER_FOLLY_H
