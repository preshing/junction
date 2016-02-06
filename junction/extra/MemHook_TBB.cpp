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

#if JUNCTION_WITH_TBB && TBB_USE_TURF_HEAP
void* tbbWrap_malloc(size_t size) {
    return TURF_HEAP.alloc(size);
}

void tbbWrap_free(void* ptr) {
    TURF_HEAP.free(ptr);
}

void* tbbWrap_padded_allocate(size_t size, size_t alignment) {
    return TURF_HEAP.allocAligned(size, alignment);
}

void tbbWrap_padded_free(void* ptr) {
    TURF_HEAP.free(ptr);
}
#endif // JUNCTION_WITH_TBB && TBB_USE_TURF_HEAP
