/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_EXTRA_IMPL_MAPADAPTER_CDS_MICHAEL_H
#define JUNCTION_EXTRA_IMPL_MAPADAPTER_CDS_MICHAEL_H

#include <junction/Core.h>

#if !JUNCTION_WITH_CDS
#error "You must configure with JUNCTION_WITH_CDS=1!"
#endif

#include <junction/MapTraits.h>
#include <cds/init.h>
#include <cds/gc/hp.h>
#include <cds/container/michael_kvlist_hp.h>
#include <cds/container/michael_map.h>

namespace junction {
namespace extra {

class MapAdapter {
public:
    static TURF_CONSTEXPR const char* getMapName() { return "CDS MichaelKVList"; }

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
        // List traits based on std::less predicate
        struct ListTraits : public cds::container::michael_list::traits {
            typedef std::less<u32> less;
        };

        // Ordered list
        typedef cds::container::MichaelKVList<cds::gc::HP, u32, void*, ListTraits> OrderedList;

        // Map traits
        struct MapTraits : public cds::container::michael_map::traits {
            struct hash {
                size_t operator()(u32 i) const {
                    return cds::opt::v::hash<u32>()(i);
                }
            };
        };

        cds::container::MichaelHashMap<cds::gc::HP, OrderedList, MapTraits> m_map;

    public:
        Map(ureg capacity) : m_map(capacity, 1) {
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

#endif // JUNCTION_EXTRA_IMPL_MAPADAPTER_CDS_MICHAEL_H
