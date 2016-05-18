/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_EXTRA_IMPL_MAPADAPTER_LINEAR_RWLOCK_H
#define JUNCTION_EXTRA_IMPL_MAPADAPTER_LINEAR_RWLOCK_H

#include <junction/Core.h>
#include <junction/SingleMap_Linear.h>
#include <turf/RWLock.h>
#include <turf/Util.h>

namespace junction {
namespace extra {

class MapAdapter {
public:
    static TURF_CONSTEXPR const char* getMapName() { return "Single + RWLock"; }

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
        turf::RWLock m_rwLock;
        SingleMap_Linear<u32, void*> m_map;

    public:
        Map(ureg capacity) : m_map(capacity) {
        }

        void assign(u32 key, void* value) {
            turf::ExclusiveLockGuard<turf::RWLock> guard(m_rwLock);
            m_map.assign(key, value);
        }

        void* get(u32 key) {
            turf::SharedLockGuard<turf::RWLock> guard(m_rwLock);
            return m_map.get(key);
        }

        void erase(u32 key) {
            turf::ExclusiveLockGuard<turf::RWLock> guard(m_rwLock);
            m_map.erase(key);
        }
    };

    static ureg getInitialCapacity(ureg maxPopulation) {
        return turf::util::roundUpPowerOf2(maxPopulation / 4);
    }
};

} // namespace extra
} // namespace junction

#endif // JUNCTION_EXTRA_IMPL_MAPADAPTER_LINEAR_RWLOCK_H
