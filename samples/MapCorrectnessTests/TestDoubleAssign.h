/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016, 2017 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef SAMPLES_MAPCORRECTNESSTESTS_TESTDOUBLEASSIGN_H
#define SAMPLES_MAPCORRECTNESSTESTS_TESTDOUBLEASSIGN_H

#include <junction/Core.h>
#include "TestEnvironment.h"
#include <turf/extra/Random.h>

class TestDoubleAssign {
public:
    static const ureg KeysToInsert = 1000;
    TestEnvironment& m_env;
    MapAdapter::Map* m_map;
    turf::Atomic<u32> m_index;

    TestDoubleAssign(TestEnvironment& env) : m_env(env), m_map(NULL), m_index(0) {
    }

    void doubleAssignKeys(ureg threadIndex) {
        for (;;) {
            u32 key = m_index.fetchAdd(1, turf::Relaxed);
            if (key >= KeysToInsert + 2)
                break;

            m_map->assign(key, (void*) (key * 20));
            m_map->erase(key);
            m_map->assign(key, (void*) (key * 20));
        }
        m_env.threads[threadIndex].update();
    }

    void checkMapContents() {
#if TEST_CHECK_MAP_CONTENTS
        for (MapAdapter::Map::Iterator iter(*m_map); iter.isValid(); iter.next()) {
            u32 key = iter.getKey();
            if (iter.getValue() != (void*) (key * 20))
                TURF_DEBUG_BREAK();
        }

        for (ureg i = 2; i < KeysToInsert + 2; i++) {
            auto r = m_map->find(i);
            if (r.getValue() != (void*) (i * 20))
                TURF_DEBUG_BREAK();
        }
#endif
    }

    void run() {
        m_map = new MapAdapter::Map(MapAdapter::getInitialCapacity(KeysToInsert));
        m_index.storeNonatomic(2);
        m_env.dispatcher.kick(&TestDoubleAssign::doubleAssignKeys, *this);
        checkMapContents();
        delete m_map;
        m_map = NULL;
    }
};

#endif // SAMPLES_MAPCORRECTNESSTESTS_TESTDOUBLEASSIGN_H
