/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016 Jeff Preshing

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/

#ifndef JUNCTION_SIMPLEJOBCOORDINATOR_H
#define JUNCTION_SIMPLEJOBCOORDINATOR_H

#include <junction/Core.h>
#include <junction/striped/ConditionBank.h>

namespace junction {

// It's safe to call everything here from within a Job itself.
// In particular, you're allowed to particpate() recursively.
// We actually do this in ConcurrentMap_Grampa::publish() when migrating a new flattree.
class SimpleJobCoordinator {
public:
    struct Job {
        virtual ~Job() {
        }
        virtual void run() = 0;
    };

private:
    JUNCTION_STRIPED_CONDITIONBANK_DEFINE_MEMBER()
    turf::Atomic<uptr> m_job;

public:
    SimpleJobCoordinator() : m_job(uptr(NULL)) {
    }

    Job* loadConsume() const {
        return (Job*) m_job.load(turf::Consume);
    }

    void storeRelease(Job* job) {
        junction::striped::ConditionPair& pair = JUNCTION_STRIPED_CONDITIONBANK_GET(this);
        {
            turf::LockGuard<turf::Mutex> guard(pair.mutex);
            m_job.store(uptr(job), turf::Release);
        }
        pair.condVar.wakeAll();
    }

    void participate() {
        junction::striped::ConditionPair& pair = JUNCTION_STRIPED_CONDITIONBANK_GET(this);
        uptr prevJob = uptr(NULL);
        for (;;) {
            uptr job = m_job.load(turf::Consume);
            if (job == prevJob) {
                turf::LockGuard<turf::Mutex> guard(pair.mutex);
                for (;;) {
                    job = m_job.loadNonatomic(); // No concurrent writes inside lock
                    if (job != prevJob)
                        break;
                    pair.condVar.wait(guard);
                }
            }
            if (job == 1)
                return;
            reinterpret_cast<Job*>(job)->run();
            prevJob = job;
        }
    }

    void runOne(Job* job) {
        TURF_ASSERT(job != (Job*) m_job.load(turf::Relaxed));
        storeRelease(job);
        job->run();
    }

    void end() {
        junction::striped::ConditionPair& pair = JUNCTION_STRIPED_CONDITIONBANK_GET(this);
        {
            turf::LockGuard<turf::Mutex> guard(pair.mutex);
            m_job.store(1, turf::Release);
        }
        pair.condVar.wakeAll();
    }
};

} // namespace junction

#endif // JUNCTION_SIMPLEJOBCOORDINATOR_H
