/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016-2017 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#include <junction/Core.h>
#include <turf/Heap.h>
#include <junction/extra/MapAdapter.h>
#include <iostream>

using namespace turf::intTypes;

int main() {
    junction::extra::MapAdapter adapter(1);
    junction::extra::MapAdapter::ThreadContext context(adapter, 0);
    junction::extra::MapAdapter::Map map(65536);

    context.registerThread();

    ureg population = 0;
    for (ureg i = 0; i < 100; i++) {
#if TURF_USE_DLMALLOC && TURF_DLMALLOC_FAST_STATS
        std::cout << "Population=" << population << ", inUse=" << TURF_HEAP.getInUseBytes() << std::endl;
#endif
        for (; population < i * 5000; population++)
            map.assign(population + 1, (void*) ((population << 2) | 3));
    }

    context.update();
    context.unregisterThread();

    return 0;
}
