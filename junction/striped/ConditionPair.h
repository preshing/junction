/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_STRIPED_CONDITIONPAIR_H
#define JUNCTION_STRIPED_CONDITIONPAIR_H

#include <junction/Core.h>
#include <turf/Mutex.h>
#include <turf/ConditionVariable.h>

namespace junction {
namespace striped {

struct ConditionPair {
    turf::Mutex mutex;
    turf::ConditionVariable condVar;
};

} // namespace striped
} // namespace junction

#endif // JUNCTION_STRIPED_CONDITIONPAIR_H
