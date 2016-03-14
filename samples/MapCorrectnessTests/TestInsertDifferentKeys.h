/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef SAMPLES_MAPCORRECTNESSTESTS_TESTINSERTDIFFERENTKEYS_H
#define SAMPLES_MAPCORRECTNESSTESTS_TESTINSERTDIFFERENTKEYS_H

#include <junction/Core.h>
#include "TestEnvironment.h"
#include <turf/extra/Random.h>

class TestInsertDifferentKeys {
public:
    static const ureg KeysToInsert = 2048;
    TestEnvironment& m_env;
    MapAdapter::Map* m_map;
    turf::extra::Random m_random;
    u32 m_startIndex;
    u32 m_relativePrime;

    TestInsertDifferentKeys(TestEnvironment& env) : m_env(env), m_map(NULL), m_startIndex(0), m_relativePrime(0) {
    }

    void insertKeys(ureg threadIndex) {
        u32 index = m_startIndex + threadIndex * (KeysToInsert + 2);
        sreg keysRemaining = KeysToInsert;
        while (keysRemaining > 0) {
            u32 key = index * m_relativePrime;
            key = key ^ (key >> 16);
            if (key >= 2) { // Don't insert 0 or 1
                m_map->assign(key, (void*) uptr(key));
                keysRemaining--;
            }
            index++;
        }
        m_env.threads[threadIndex].update();
    }

    void eraseKeys(ureg threadIndex) {
        u32 index = m_startIndex + threadIndex * (KeysToInsert + 2);
        sreg keysRemaining = KeysToInsert;
        while (keysRemaining > 0) {
            u32 key = index * m_relativePrime;
            key = key ^ (key >> 16);
            if (key >= 2) { // Don't insert 0 or 1
                m_map->erase(key);
                keysRemaining--;
            }
            index++;
        }
        m_env.threads[threadIndex].update();
    }

    void checkMapContents() {
#if TEST_CHECK_MAP_CONTENTS
        ureg iterCount = 0;
        ureg iterChecksum = 0;
        for (MapAdapter::Map::Iterator iter(*m_map); iter.isValid(); iter.next()) {
            iterCount++;
            u32 key = iter.getKey();
            iterChecksum += key;
            if (iter.getValue() != (void*) uptr(key))
                TURF_DEBUG_BREAK();
        }

        ureg actualChecksum = 0;
        for (ureg i = 0; i < m_env.numThreads; i++) {
            u32 index = m_startIndex + i * (KeysToInsert + 2);
            sreg leftToCheck = KeysToInsert;
            while (leftToCheck > 0) {
                u32 key = index * m_relativePrime;
                key = key ^ (key >> 16);
                if (key >= 2) { // Don't insert 0 or 1
                    if (m_map->get(key) != (void*) uptr(key))
                        TURF_DEBUG_BREAK();
                    actualChecksum += key;
                    leftToCheck--;
                }
                index++;
            }
        }

        if (iterCount != KeysToInsert * m_env.numThreads)
            TURF_DEBUG_BREAK();
        if (iterChecksum != actualChecksum)
            TURF_DEBUG_BREAK();
#endif
    }

    void checkMapEmpty() {
#if TEST_CHECK_MAP_CONTENTS
        for (MapAdapter::Map::Iterator iter(*m_map); iter.isValid(); iter.next()) {
            TURF_DEBUG_BREAK();
        }

        for (ureg i = 0; i < m_env.numThreads; i++) {
            u32 index = m_startIndex + i * (KeysToInsert + 2);
            sreg leftToCheck = KeysToInsert;
            while (leftToCheck > 0) {
                u32 key = index * m_relativePrime;
                key = key ^ (key >> 16);
                if (key >= 2) { // Don't insert 0 or 1
                    if (m_map->get(key))
                        TURF_DEBUG_BREAK();
                    leftToCheck--;
                }
                index++;
            }
        }
#endif // TEST_CHECK_MAP_CONTENTS
    }

    void run() {
        m_map = new MapAdapter::Map(MapAdapter::getInitialCapacity(KeysToInsert));
        m_startIndex = m_random.next32();
        m_relativePrime = m_random.next32() * 2 + 1;
        m_env.dispatcher.kick(&TestInsertDifferentKeys::insertKeys, *this);
        checkMapContents();
        m_env.dispatcher.kick(&TestInsertDifferentKeys::eraseKeys, *this);
        checkMapEmpty();
        delete m_map;
        m_map = NULL;
    }
};

#endif // SAMPLES_MAPCORRECTNESSTESTS_TESTINSERTDIFFERENTKEYS_H
