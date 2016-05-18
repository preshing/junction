/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_EXTRA_IMPL_MAPADAPTER_STDMAP_H
#define JUNCTION_EXTRA_IMPL_MAPADAPTER_STDMAP_H

#include <junction/Core.h>
#include <map>
#include <mutex>

namespace junction {
namespace extra {

class MapAdapter {
public:
    static TURF_CONSTEXPR const char* getMapName() { return "std::map + std::mutex"; }

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
        std::mutex m_mutex;
        typedef std::map<u32, void*> MapType;
        MapType m_map;

    public:
        Map(ureg) {
        }

        void assign(u32 key, void* value) {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_map[key] = value;
        }

        void* get(u32 key) {
            std::lock_guard<std::mutex> guard(m_mutex);
            MapType::iterator iter = m_map.find(key);
            return (iter == m_map.end()) ? NULL : iter->second;
        }

        void erase(u32 key) {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_map.erase(key);
        }

        class Iterator {
        private:
            Map& m_map;
            MapType::iterator m_iter;

        public:
            Iterator(Map& map) : m_map(map), m_iter(m_map.m_map.begin()) {
            }

            void next() {
                m_iter++;
            }

            bool isValid() const {
                return m_iter != m_map.m_map.end();
            }

            u32 getKey() const {
                return m_iter->first;
            }

            void* getValue() const {
                return m_iter->second;
            }
        };
    };

    static ureg getInitialCapacity(ureg) {
        return 0;
    }
};

} // namespace extra
} // namespace junction

#endif // JUNCTION_EXTRA_IMPL_MAPADAPTER_STDMAP_H
