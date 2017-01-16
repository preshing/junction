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
#include <junction/striped/ConditionBank.h>

#if JUNCTION_USE_STRIPING

namespace junction {
namespace striped {

ConditionBank DefaultConditionBank;

ConditionBank::~ConditionBank() {
    m_initSpinLock.lock();
    ConditionPair* pairs = m_pairs.exchange(nullptr, turf::ConsumeRelease);
    delete [] pairs;
    m_initSpinLock.unlock();
}

ConditionPair* ConditionBank::initialize() {
    m_initSpinLock.lock();
    ConditionPair* pairs = m_pairs.loadNonatomic();
    if (!pairs) {
        pairs = new ConditionPair[SizeMask + 1];
        m_pairs.store(pairs, turf::Release);
    }
    m_initSpinLock.unlock();
    return pairs;
}

} // namespace striped
} // namespace junction

#endif // JUNCTION_USE_STRIPING
