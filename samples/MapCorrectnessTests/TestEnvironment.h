/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef SAMPLES_MAPCORRECTNESSTESTS_TESTENVIRONMENT_H
#define SAMPLES_MAPCORRECTNESSTESTS_TESTENVIRONMENT_H

#include <junction/Core.h>
#include <turf/extra/JobDispatcher.h>
#include <turf/Trace.h>
#include <junction/extra/MapAdapter.h>

using namespace turf::intTypes;
typedef junction::extra::MapAdapter MapAdapter;

struct TestEnvironment {
    turf::extra::JobDispatcher dispatcher;
    ureg numThreads;
    MapAdapter adapter;
    std::vector<MapAdapter::ThreadContext> threads;

    TestEnvironment() : numThreads(dispatcher.getNumPhysicalCores()), adapter(numThreads) {
        TURF_ASSERT(numThreads > 0);
        for (ureg t = 0; t < numThreads; t++)
            threads.push_back(MapAdapter::ThreadContext(adapter, t));
        dispatcher.kickMulti(&MapAdapter::ThreadContext::registerThread, &threads[0], threads.size());
    }

    ~TestEnvironment() {
        dispatcher.kickMulti(&MapAdapter::ThreadContext::unregisterThread, &threads[0], threads.size());
    }
};

#endif // SAMPLES_MAPCORRECTNESSTESTS_TESTENVIRONMENT_H
