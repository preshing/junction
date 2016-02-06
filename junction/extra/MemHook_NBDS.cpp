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

#if JUNCTION_WITH_NBDS && NBDS_USE_TURF_HEAP
extern "C" {
void mem_init(void) {
}

void* nbd_malloc(size_t n) {
    return TURF_HEAP.alloc(n);
}

void nbd_free(void* x) {
    TURF_HEAP.free(x);
}
} // extern "C"
#endif // JUNCTION_WITH_NBDS && NBDS_USE_TURF_HEAP
