/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_EXTRA_IMPL_MAPADAPTER_LINEAR_H
#define JUNCTION_EXTRA_IMPL_MAPADAPTER_LINEAR_H

#include <junction/Core.h>
#include <junction/QSBR.h>
#include <junction/ConcurrentMap_Linear.h>
#include <turf/Util.h>

namespace junction {
namespace extra {

class MapAdapter {
public:
    static TURF_CONSTEXPR const char* getMapName() { return "Junction Linear map"; }

    MapAdapter(ureg) {
    }

    class ThreadContext {
    private:
        QSBR::Context m_qsbrContext;

    public:
        ThreadContext(MapAdapter&, ureg) {
        }

        void registerThread() {
            m_qsbrContext = DefaultQSBR.createContext();
        }

        void unregisterThread() {
            DefaultQSBR.destroyContext(m_qsbrContext);
        }

        void update() {
            DefaultQSBR.update(m_qsbrContext);
        }
    };

    typedef ConcurrentMap_Linear<u32, void*> Map;

    static ureg getInitialCapacity(ureg maxPopulation) {
        return turf::util::roundUpPowerOf2(maxPopulation / 4);
    }
};

} // namespace extra
} // namespace junction

#endif // JUNCTION_EXTRA_IMPL_MAPADAPTER_LINEAR_H
