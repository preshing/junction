/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_STRIPED_MANUALRESETEVENT_H
#define JUNCTION_STRIPED_MANUALRESETEVENT_H

#include <junction/Core.h>

#if JUNCTION_USE_STRIPING

//-----------------------------------
// Striping enabled
//-----------------------------------
#include <junction/striped/ConditionBank.h>

namespace junction {
namespace striped {

class ManualResetEvent {
private:
    JUNCTION_STRIPED_CONDITIONBANK_DEFINE_MEMBER()
    static const u8 Signaled = 1;
    static const u8 HasWaiters = 2;
    turf::Atomic<u8> m_state;

public:
    ManualResetEvent(bool initialState = false) : m_state(initialState ? Signaled : 0) {
    }

    ~ManualResetEvent() {
    }

    void signal() {
        u8 prevState = m_state.fetchOr(Signaled, turf::Release); // Synchronizes-with the load in wait (fast path)
        if (prevState & HasWaiters) {
            ConditionPair& pair = JUNCTION_STRIPED_CONDITIONBANK_GET(this);
            turf::LockGuard<turf::Mutex> guard(
                pair.mutex); // Prevents the wake from occuring in the middle of wait()'s critical section
            pair.condVar.wakeAll();
        }
    }

    bool isSignaled() const {
        return m_state.load(turf::Relaxed) & Signaled;
    }

    void reset() {
        TURF_ASSERT(0); // FIXME: implement it
    }

    void wait() {
        u8 state = m_state.load(turf::Acquire); // Synchronizes-with the fetchOr in signal (fast path)
        if ((state & Signaled) == 0) {
            ConditionPair& pair = JUNCTION_STRIPED_CONDITIONBANK_GET(this);
            turf::LockGuard<turf::Mutex> guard(pair.mutex);
            for (;;) {
                // FIXME: Implement reusable AdaptiveBackoff class and apply it here
                state = m_state.load(turf::Relaxed);
                if (state & Signaled)
                    break;
                if (state != HasWaiters) {
                    TURF_ASSERT(state == 0);
                    if (!m_state.compareExchangeWeak(state, HasWaiters, turf::Relaxed, turf::Relaxed))
                        continue;
                }
                // The lock ensures signal can't wakeAll between the load and the wait
                pair.condVar.wait(guard);
            }
        }
    }
};

} // namespace striped
} // namespace junction

#else // JUNCTION_USE_STRIPING

//-----------------------------------
// Striping disabled
//-----------------------------------
#include <turf/ManualResetEvent.h>

namespace junction {
namespace striped {
typedef turf::ManualResetEvent ManualResetEvent;
} // namespace striped
} // namespace junction

#endif // JUNCTION_USE_STRIPING

#endif // JUNCTION_STRIPED_MANUALRESETEVENT_H
