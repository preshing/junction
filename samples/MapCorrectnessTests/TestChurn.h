/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef SAMPLES_MAPCORRECTNESSTESTS_TESTCHURN_H
#define SAMPLES_MAPCORRECTNESSTESTS_TESTCHURN_H

#include <junction/Core.h>
#include "TestEnvironment.h"
#include <turf/extra/Random.h>
#include <vector>
#include <turf/Util.h>

class TestChurn {
public:
    static const ureg KeysInBlock = 32;
    static const ureg BlocksToMaintain = 256;
    static const ureg BlocksToLookup = 4;
    static const ureg StepsPerIteration = 100;

    enum Phase {
        Phase_Insert,
        Phase_Lookup,
        Phase_Erase,
        Phase_LookupDeleted,
    };

    struct ThreadInfo {
        turf::extra::Random random;
        u32 rangeLo;
        u32 rangeHi;
        u32 insertIndex;
        u32 eraseIndex;
        u32 lookupIndex;
        Phase phase;
        ureg keysToCheck;
    };

    TestEnvironment& m_env;
    MapAdapter::Map m_map;
    u32 m_rangePerThread;
    u32 m_relativePrime;
    std::vector<ThreadInfo> m_threads;

    TestChurn(TestEnvironment& env)
        : m_env(env), m_map(MapAdapter::getInitialCapacity(KeysInBlock * BlocksToMaintain * env.numThreads)) {
        m_threads.resize(m_env.numThreads);
        m_rangePerThread = u32(-3) / m_env.numThreads; // from 2 to 0xffffffff inclusive
        TURF_ASSERT(KeysInBlock * (BlocksToMaintain + BlocksToLookup + 1) < m_rangePerThread);
        u32 startIndex = 2;
        for (ureg i = 0; i < m_env.numThreads; i++) {
            ThreadInfo& thread = m_threads[i];
            thread.rangeLo = startIndex;
            startIndex += m_rangePerThread;
            thread.rangeHi = startIndex;
            thread.insertIndex = thread.rangeLo + thread.random.next32() % m_rangePerThread;
            thread.eraseIndex = thread.insertIndex;
            thread.lookupIndex = 0;
            thread.phase = Phase_Insert;
            thread.keysToCheck = 0;
        }
        m_relativePrime = m_threads[0].random.next32() * 2 + 1;
        m_env.dispatcher.kick(&TestChurn::warmUp, *this);
    }

    void warmUp(ureg threadIndex) {
        ThreadInfo& thread = m_threads[threadIndex];
        TURF_ASSERT(thread.phase == Phase_Insert);
        TURF_ASSERT(thread.insertIndex == thread.eraseIndex);
        for (sreg keysRemaining = KeysInBlock * BlocksToMaintain; keysRemaining > 0; keysRemaining--) {
            u32 key = thread.insertIndex * m_relativePrime;
            key = key ^ (key >> 16);
            if (key >= 2) {
                m_map.assign(key, (void*) uptr(key));
            }
            if (++thread.insertIndex >= thread.rangeHi)
                thread.insertIndex = thread.rangeLo;
        }
    }

    void doChurn(ureg threadIndex) {
        ThreadInfo& thread = m_threads[threadIndex];
        TURF_ASSERT(thread.insertIndex != thread.eraseIndex);
        for (sreg stepsRemaining = StepsPerIteration; stepsRemaining > 0; stepsRemaining--) {
            switch (thread.phase) {
            case Phase_Insert: {
                for (sreg keysRemaining = KeysInBlock; keysRemaining > 0; keysRemaining--) {
                    u32 key = thread.insertIndex * m_relativePrime;
                    key = key ^ (key >> 16);
                    if (key >= 2) {
                        m_map.assign(key, (void*) uptr(key));
                    }
                    if (++thread.insertIndex >= thread.rangeHi)
                        thread.insertIndex = thread.rangeLo;
                    TURF_ASSERT(thread.insertIndex != thread.eraseIndex);
                }
                thread.phase = Phase_Lookup;
                thread.lookupIndex = thread.insertIndex;
                thread.keysToCheck = KeysInBlock + (thread.random.next32() % (KeysInBlock * (BlocksToLookup - 1)));
                break;
            }
            case Phase_Lookup: {
                sreg keysRemaining = turf::util::min(thread.keysToCheck, KeysInBlock);
                thread.keysToCheck -= keysRemaining;
                for (; keysRemaining > 0; keysRemaining--) {
                    if (thread.lookupIndex == thread.rangeLo)
                        thread.lookupIndex = thread.rangeHi;
                    thread.lookupIndex--;
                    u32 key = thread.lookupIndex * m_relativePrime;
                    key = key ^ (key >> 16);
                    if (key >= 2) {
                        if (m_map.get(key) != (void*) uptr(key))
                            TURF_DEBUG_BREAK();
                    }
                }
                if (thread.keysToCheck == 0) {
                    thread.phase = Phase_Erase;
                }
                break;
            }
            case Phase_Erase: {
                for (sreg keysRemaining = KeysInBlock; keysRemaining > 0; keysRemaining--) {
                    u32 key = thread.eraseIndex * m_relativePrime;
                    key = key ^ (key >> 16);
                    if (key >= 2) {
                        m_map.erase(key);
                    }
                    if (++thread.eraseIndex >= thread.rangeHi)
                        thread.eraseIndex = thread.rangeLo;
                    TURF_ASSERT(thread.insertIndex != thread.eraseIndex);
                }
                thread.phase = Phase_LookupDeleted;
                thread.lookupIndex = thread.eraseIndex;
                thread.keysToCheck = KeysInBlock + (thread.random.next32() % (KeysInBlock * (BlocksToLookup - 1)));
                break;
            }
            case Phase_LookupDeleted: {
                sreg keysRemaining = turf::util::min(thread.keysToCheck, KeysInBlock);
                thread.keysToCheck -= keysRemaining;
                for (; keysRemaining > 0; keysRemaining--) {
                    if (thread.lookupIndex == thread.rangeLo)
                        thread.lookupIndex = thread.rangeHi;
                    thread.lookupIndex--;
                    u32 key = thread.lookupIndex * m_relativePrime;
                    key = key ^ (key >> 16);
                    if (key >= 2) {
                        if (m_map.get(key))
                            TURF_DEBUG_BREAK();
                    }
                }
                if (thread.keysToCheck == 0) {
                    thread.phase = Phase_Insert;
                }
                break;
            }
            }
        }
        m_env.threads[threadIndex].update();
    }

    void run() {
        m_env.dispatcher.kick(&TestChurn::doChurn, *this);
    }
};

#endif // SAMPLES_MAPCORRECTNESSTESTS_TESTCHURN_H
