/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_EXTRA_IMPL_MAPADAPTER_CRUDE_H
#define JUNCTION_EXTRA_IMPL_MAPADAPTER_CRUDE_H

#include <junction/Core.h>
#include <junction/ConcurrentMap_Crude.h>
#include <turf/Util.h>

namespace junction {
namespace extra {

class MapAdapter {
public:
    static TURF_CONSTEXPR const char* getMapName() { return "Junction Crude map"; }

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

    typedef ConcurrentMap_Crude<u32, void*> Map;

    static ureg getInitialCapacity(ureg maxPopulation) {
        return turf::util::roundUpPowerOf2(ureg(maxPopulation * 1.25f));
    }
};

} // namespace extra
} // namespace junction

#endif // JUNCTION_EXTRA_IMPL_MAPADAPTER_CRUDE_H
