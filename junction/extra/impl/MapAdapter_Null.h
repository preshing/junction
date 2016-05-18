/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_EXTRA_IMPL_MAPADAPTER_NULL_H
#define JUNCTION_EXTRA_IMPL_MAPADAPTER_NULL_H

#include <junction/Core.h>

namespace junction {
namespace extra {

class MapAdapter {
public:
    static TURF_CONSTEXPR const char* getMapName() { return "Null"; }

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
    public:
        Map(ureg) {
        }

        void assign(u32, void*) {
        }

        void* get(u32) {
            return NULL;
        }

        void erase(u32) {
        }
    };

    static ureg getInitialCapacity(ureg) {
        return 0;
    }
};

} // namespace extra
} // namespace junction

#endif // JUNCTION_EXTRA_IMPL_MAPADAPTER_NULL_H
