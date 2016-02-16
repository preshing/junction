/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#include <junction/Core.h>
#include <turf/extra/JobDispatcher.h>
#include <junction/extra/MapAdapter.h>
#include <turf/extra/Random.h>
#include <stdio.h>

using namespace turf::intTypes;
typedef junction::extra::MapAdapter MapAdapter;

turf::extra::Random g_random[4];

class StoreBufferTest {
public:
    MapAdapter::Map m_map;
    // Keys:
    u32 X;
    u32 Y;
    // Observed values:
    uptr m_r1;
    uptr m_r2;

    StoreBufferTest() : m_map(1024) {
        // Randomize the keys
        do {
            X = g_random[0].next32();
        } while (X == 0);
        do {
            Y = g_random[0].next32();
        } while (Y == 0 || Y == X);
    }

    void run(ureg threadIndex) {
        u32 x = X;
        u32 y = Y;
        while ((g_random[threadIndex].next32() & 0x1ff) != 0) { // Random delay
        }
        if (threadIndex == 0) {
            // We store 2 because 1 is reserved for the default Redirect value.
            // The default can be overridden, but this is easier.
            m_map.insert(x, (void*) 2);
            m_r1 = (uptr) m_map.get(y);
        } else {
            m_map.insert(y, (void*) 2);
            m_r2 = (uptr) m_map.get(x);
        }
    }
};

class IRIWTest {
public:
    MapAdapter::Map m_map;
    // Keys:
    u32 X;
    u32 Y;
    // Observed values:
    uptr m_r1;
    uptr m_r2;
    uptr m_r3;
    uptr m_r4;

    IRIWTest() : m_map(1024) {
        // Randomize the keys
        do {
            X = g_random[0].next32();
        } while (X == 0);
        do {
            Y = g_random[0].next32();
        } while (Y == 0 || Y == X);
    }

    void run(ureg threadIndex) {
        u32 x = X;
        u32 y = Y;
        while ((g_random[threadIndex].next32() & 0x7f) != 0) { // Random delay
        }
        switch (threadIndex) {
        case 0:
            // We store 2 because 1 is reserved for the default Redirect value.
            // The default can be overridden, but this is easier.
            m_map.insert(x, (void*) 2);
            break;
            
        case 1:
            m_r1 = (uptr) m_map.get(x);
            m_r2 = (uptr) m_map.get(y);
            break;
            
        case 2:
            m_map.insert(y, (void*) 2);
            break;
            
        case 3:
            m_r3 = (uptr) m_map.get(y);
            m_r4 = (uptr) m_map.get(x);
            break;
        }
    }
};

int main() {
#if 1
    turf::extra::JobDispatcher dispatcher(4);
    MapAdapter adapter(4);

    int nonLinearizable = 0;
    for (int iterations = 0;; iterations++) {
        IRIWTest test;
        dispatcher.kick(&IRIWTest::run, test);
//        printf("%d %d %d %d\n", test.m_r1, test.m_r2, test.m_r3, test.m_r4);
        if ((test.m_r1 == 2 && test.m_r2 == 0 && test.m_r3 == 2 && test.m_r4 == 0)
            || (test.m_r1 == 0 && test.m_r2 == 2 && test.m_r3 == 0 && test.m_r4 == 2))
            nonLinearizable++;
        if (iterations % 10000 == 0) {
            printf("%d non-linearizable histories after %d iterations\n", nonLinearizable, iterations);
        }
    }
#else
    turf::extra::JobDispatcher dispatcher(2);
    MapAdapter adapter(2);

    u64 nonLinearizable = 0;
    for (u64 iterations = 0;; iterations++) {
        StoreBufferTest test;
        dispatcher.kick(&StoreBufferTest::run, test);
        if (test.m_r1 == 0 && test.m_r2 == 0)
            nonLinearizable++;
        if (iterations % 10000 == 0) {
            printf("%" TURF_U64D " non-linearizable histories after %" TURF_U64D " iterations\n", nonLinearizable, iterations);
        }
    }
#endif

    return 0;
}
