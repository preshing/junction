/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_EXTRA_IMPL_MAPADAPTER_CDS_CUCKOO_H
#define JUNCTION_EXTRA_IMPL_MAPADAPTER_CDS_CUCKOO_H

#include <junction/Core.h>

#if !JUNCTION_WITH_CDS
#error "You must configure with JUNCTION_WITH_CDS=1!"
#endif

#include <junction/MapTraits.h>
#include <cds/init.h>
#include <cds/gc/hp.h>
#include <memory.h> // memcpy required by cuckoo_map.h
#include <cds/container/cuckoo_map.h>

namespace junction {
namespace extra {

class MapAdapter {
public:
    static TURF_CONSTEXPR const char* getMapName() { return "CDS CuckooMap"; }

    cds::gc::HP* m_hpGC;

    MapAdapter(ureg) {
        cds::Initialize();
        m_hpGC = new cds::gc::HP;
    }

    ~MapAdapter() {
        delete m_hpGC;
        cds::Terminate();
    }

    class ThreadContext {
    public:
        ThreadContext(MapAdapter&, ureg) {
        }

        void registerThread() {
            cds::threading::Manager::attachThread();
        }

        void unregisterThread() {
            cds::threading::Manager::detachThread();
        }

        void update() {
        }
    };

    class Map {
    private:
        struct Hash1 {
            size_t operator()(u32 s) const {
                return junction::hash(s);
            }
        };

        struct Hash2 {
            size_t operator()(u32 s) const {
                return junction::hash(s + 0x9e3779b9);
            }
        };

        struct Traits : cds::container::cuckoo::traits {
            typedef std::equal_to<u32> equal_to;
            typedef cds::opt::hash_tuple<Hash1, Hash2> hash;
        };

        cds::container::CuckooMap<u32, void*, Traits> m_map;

    public:
        Map(ureg capacity) : m_map() {
        }

        void assign(u32 key, void* value) {
            m_map.insert(key, value);
        }

        void* get(u32 key) {
            void* result = NULL;
            m_map.find(key, [&result](std::pair<const u32, void*>& item) { result = item.second; });
            return result;
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

#endif // JUNCTION_EXTRA_IMPL_MAPADAPTER_CDS_CUCKOO_H
