/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_EXTRA_IMPL_MAPADAPTER_NBDS_H
#define JUNCTION_EXTRA_IMPL_MAPADAPTER_NBDS_H

#include <junction/Core.h>

#if !JUNCTION_WITH_NBDS
#error "You must configure with JUNCTION_WITH_NBDS=1!"
#endif

extern "C" {
#include <runtime.h>
#include <rcu.h>
#include <../runtime/rlocal.h>
#include <hashtable.h>
}

namespace junction {
namespace extra {

class MapAdapter {
public:
    static TURF_CONSTEXPR const char* getMapName() { return "nbds hashtable_t"; }

    MapAdapter(ureg) {
    }

    class ThreadContext {
    private:
        ureg m_threadIndex;

    public:
        ThreadContext(MapAdapter&, ureg threadIndex) : m_threadIndex(threadIndex) {
        }

        void registerThread() {
            rcu_thread_init(m_threadIndex);
        }

        void unregisterThread() {
        }

        void update() {
            rcu_update();
        }
    };

    class Map {
    private:
        hashtable_t* m_map;

    public:
        Map(ureg) {
            m_map = ht_alloc(NULL);
        }

        ~Map() {
            ht_free(m_map);
        }

        void assign(u32 key, void* value) {
            ht_cas(m_map, key, CAS_EXPECT_WHATEVER, (map_val_t) value);
        }

        void* get(u32 key) {
            return (void*) ht_get(m_map, key);
        }

        void erase(u32 key) {
            ht_remove(m_map, key);
        }
    };

    static ureg getInitialCapacity(ureg) {
        return 0;
    }
};

} // namespace extra
} // namespace junction

#endif // JUNCTION_EXTRA_IMPL_MAPADAPTER_NBDS_H
