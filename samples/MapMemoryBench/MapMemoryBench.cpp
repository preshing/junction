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
#include <turf/Heap.h>
#include <turf/Util.h>
#include <stdio.h>
#include <junction/extra/MapAdapter.h>

#if TURF_USE_DLMALLOC && TURF_DLMALLOC_FAST_STATS

using namespace turf::intTypes;
typedef junction::extra::MapAdapter MapAdapter;

static const ureg MaxPopulation = 1000000;
static const ureg MinPopulation = 1000;
static const ureg StepSize = 500;
static const u32 Prime = 0x4190ab09;

int main() {
    MapAdapter adapter(1);
    MapAdapter::ThreadContext threadCtx(adapter, 0);
    threadCtx.registerThread();

    ureg startMem = TURF_HEAP.getInUseBytes();
    ureg mem = 0;
    ureg initialCapacity = MapAdapter::getInitialCapacity(MaxPopulation);
    MapAdapter::Map* map = new MapAdapter::Map(initialCapacity);

    printf("[\n");
    ureg population = 0;
    while (population < MaxPopulation) {
        ureg loMem = mem;
        ureg hiMem = mem;
        for (ureg target = population + StepSize; population < target; population++) {
            u32 key = u32(population + 1) * Prime;
            if (key >= 2) {
                map->assign(key, (void*) uptr(key));
                population++;
                threadCtx.update();
                ureg inUseBytes = TURF_HEAP.getInUseBytes();
                mem = inUseBytes - startMem;
                loMem = turf::util::min(loMem, mem);
                hiMem = turf::util::max(hiMem, mem);
            }
        }
        printf("    (%d, %d, %d),\n", (int) population, (int) loMem, (int) hiMem);
    }
    printf("]\n");

    delete map;
    return 0;
}

#else // TURF_USE_DLMALLOC && TURF_DLMALLOC_FAST_STATS

int main() {
    fprintf(stderr, "Must configure with TURF_USE_DLMALLOC=1 and TURF_DLMALLOC_FAST_STATS=1\n");
    return 1;
}

#endif // TURF_USE_DLMALLOC && TURF_DLMALLOC_FAST_STATS
