/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_STRIPED_MUTEX_H
#define JUNCTION_STRIPED_MUTEX_H

#include <junction/Core.h>

#if JUNCTION_USE_STRIPING

//-----------------------------------
// Striping enabled
//-----------------------------------
#include <junction/striped/AutoResetEvent.h>

namespace junction {
namespace striped {

// Not recursive
class Mutex {
private:
    turf::Atomic<sreg> m_status;
    junction::striped::AutoResetEvent m_event;

    void lockSlow() {
        while (m_status.exchange(1, turf::Acquire) >= 0)
            m_event.wait();
    }

public:
    Mutex() : m_status(-1), m_event(false) {
    }

    void lock() {
        if (m_status.exchange(0, turf::Acquire) >= 0)
            lockSlow();
    }

    bool tryLock() {
        return (m_status.compareExchange(-1, 0, turf::Acquire) < 0);
    }

    void unlock() {
        if (m_status.exchange(-1, turf::Release) > 0)
            m_event.signal();
    }
};

} // namespace striped
} // namespace junction

#else // JUNCTION_USE_STRIPING

//-----------------------------------
// Striping disabled
//-----------------------------------
#include <turf/Mutex.h>

namespace junction {
namespace striped {
typedef turf::Mutex Mutex;
} // namespace striped
} // namespace junction

#endif // JUNCTION_USE_STRIPING

#endif // JUNCTION_STRIPED_MUTEX_H
