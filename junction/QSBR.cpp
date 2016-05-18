/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#include <junction/QSBR.h>
#include <turf/Thread.h>
#include <turf/Mutex.h>
#include <turf/RaceDetector.h>
#include <vector>

namespace junction {

QSBR DefaultQSBR;

QSBR::Context QSBR::createContext() {
    turf::LockGuard<turf::Mutex> guard(m_mutex);
    TURF_RACE_DETECT_GUARD(m_flushRaceDetector);
    m_numContexts++;
    m_remaining++;
    TURF_ASSERT(m_numContexts < (1 << 14));
    sreg context = m_freeIndex;
    if (context >= 0) {
        TURF_ASSERT(context < (sreg) m_status.size());
        TURF_ASSERT(!m_status[context].inUse);
        m_freeIndex = m_status[context].nextFree;
        m_status[context] = Status();
    } else {
        context = m_status.size();
        m_status.push_back(Status());
    }
    return context;
}

void QSBR::destroyContext(QSBR::Context context) {
    std::vector<Action> actions;
    {
        turf::LockGuard<turf::Mutex> guard(m_mutex);
        TURF_RACE_DETECT_GUARD(m_flushRaceDetector);
        TURF_ASSERT(context < m_status.size());
        if (m_status[context].inUse && !m_status[context].wasIdle) {
            TURF_ASSERT(m_remaining > 0);
            --m_remaining;
        }
        m_status[context].inUse = 0;
        m_status[context].nextFree = m_freeIndex;
        m_freeIndex = context;
        m_numContexts--;
        if (m_remaining == 0)
            onAllQuiescentStatesPassed(actions);
    }
    for (ureg i = 0; i < actions.size(); i++)
        actions[i]();
}

void QSBR::onAllQuiescentStatesPassed(std::vector<Action>& actions) {
    // m_mutex must be held
    actions.swap(m_pendingActions);
    m_pendingActions.swap(m_deferredActions);
    m_remaining = m_numContexts;
    for (ureg i = 0; i < m_status.size(); i++)
        m_status[i].wasIdle = 0;
}

void QSBR::update(QSBR::Context context) {
    std::vector<Action> actions;
    {
        turf::LockGuard<turf::Mutex> guard(m_mutex);
        TURF_RACE_DETECT_GUARD(m_flushRaceDetector);
        TURF_ASSERT(context < m_status.size());
        Status& status = m_status[context];
        TURF_ASSERT(status.inUse);
        if (status.wasIdle)
            return;
        status.wasIdle = 1;
        TURF_ASSERT(m_remaining > 0);
        if (--m_remaining > 0)
            return;
        onAllQuiescentStatesPassed(actions);
    }
    for (ureg i = 0; i < actions.size(); i++)
        actions[i]();
}

void QSBR::flush() {
    // This is like saying that all contexts are quiescent,
    // so we can issue all actions at once.
    // No lock is taken.
    TURF_RACE_DETECT_GUARD(m_flushRaceDetector); // There should be no concurrent operations
    for (ureg i = 0; i < m_pendingActions.size(); i++)
        m_pendingActions[i]();
    m_pendingActions.clear();
    for (ureg i = 0; i < m_deferredActions.size(); i++)
        m_deferredActions[i]();
    m_deferredActions.clear();
    m_remaining = m_numContexts;
}

} // namespace junction
