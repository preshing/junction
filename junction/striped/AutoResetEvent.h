/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_STRIPED_AUTORESETEVENT_H
#define JUNCTION_STRIPED_AUTORESETEVENT_H

#include <junction/Core.h>
#include <junction/striped/ConditionBank.h>

namespace junction {
namespace striped {

class AutoResetEvent {
private:
    JUNCTION_STRIPED_CONDITIONBANK_DEFINE_MEMBER()
    bool m_status;

public:
    AutoResetEvent(bool status) : m_status(status) {
    }

    void wait() {
        ConditionPair& pair = JUNCTION_STRIPED_CONDITIONBANK_GET(this);
        turf::LockGuard<turf::Mutex> guard(pair.mutex);
        while (!m_status)
            pair.condVar.wait(guard);
        m_status = false;
    }

    void signal() {
        ConditionPair& pair = JUNCTION_STRIPED_CONDITIONBANK_GET(this);
        turf::LockGuard<turf::Mutex> guard(pair.mutex);
        if (!m_status) {
            m_status = true;
            // FIXME: Is there a more efficient striped::Mutex implementation that is safe from deadlock?
            // This approach will wake up too many threads when there is heavy contention.
            // However, if we don't wakeAll(), we will miss wakeups, since ConditionPairs are shared.
            pair.condVar.wakeAll();
        }
    }
};

} // namespace striped
} // namespace junction

#endif // JUNCTION_STRIPED_AUTORESETEVENT_H
