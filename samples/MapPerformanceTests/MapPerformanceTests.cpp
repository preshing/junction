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
#include <turf/CPUTimer.h>
#include <turf/Util.h>
#include <turf/extra/UniqueSequence.h>
#include <turf/extra/JobDispatcher.h>
#include <turf/extra/Options.h>
#include <junction/extra/MapAdapter.h>
#include <algorithm>
#include <vector>
#include <stdio.h>

using namespace turf::intTypes;
typedef junction::extra::MapAdapter MapAdapter;

static const ureg NumKeysPerThread = 16384;
static const ureg DefaultReadsPerWrite = 19;
static const ureg DefaultItersPerChunk = 128;
static const ureg DefaultChunks = 10;
static const u32 Prime = 0x4190ab09;

class Delay {
private:
    turf::extra::Random m_rand;
    u32 m_threshold;

public:
    Delay(float ratio) {
        m_threshold = u32(double(0xffffffffu) * ratio);
    }

    void delay(ureg& workUnits) {
        for (;;) {
            volatile ureg v = m_rand.next32();
            workUnits++;
            if (v <= m_threshold)
                break;
        }
    }
};

struct SharedState {
    MapAdapter& adapter;
    MapAdapter::Map* map;
    ureg numKeysPerThread;
    float delayFactor;
    ureg numThreads;
    ureg readsPerWrite;
    ureg itersPerChunk;
    turf::extra::SpinKicker spinKicker;
    turf::Atomic<u32> doneFlag;

    SharedState(MapAdapter& adapter, ureg numThreads, ureg numKeysPerThread, ureg readsPerWrite, ureg itersPerChunk)
        : adapter(adapter), map(NULL), numKeysPerThread(numKeysPerThread), numThreads(numThreads), readsPerWrite(readsPerWrite),
          itersPerChunk(itersPerChunk) {
        delayFactor = 0.5f;
        doneFlag.storeNonatomic(0);
    }
};

class ThreadState {
public:
    SharedState* m_shared;
    MapAdapter::ThreadContext m_threadCtx;
    ureg m_threadIndex;
    u32 m_rangeLo;
    u32 m_rangeHi;

    u32 m_addIndex;
    u32 m_removeIndex;

    struct Stats {
        ureg workUnitsDone;
        ureg mapOpsDone;
        double duration;

        Stats() {
            workUnitsDone = 0;
            mapOpsDone = 0;
            duration = 0;
        }

        Stats& operator+=(const Stats& other) {
            workUnitsDone += other.workUnitsDone;
            mapOpsDone += other.mapOpsDone;
            duration += other.duration;
            return *this;
        }

        bool operator<(const Stats& other) const {
            return duration < other.duration;
        }
    };

    Stats m_stats;

    ThreadState(SharedState* shared, ureg threadIndex, u32 rangeLo, u32 rangeHi)
        : m_shared(shared), m_threadCtx(shared->adapter, threadIndex) {
        m_threadIndex = threadIndex;
        m_rangeLo = rangeLo;
        m_rangeHi = rangeHi;
        m_addIndex = rangeLo;
        m_removeIndex = rangeLo;
    }

    void registerThread() {
        m_threadCtx.registerThread();
    }

    void unregisterThread() {
        m_threadCtx.unregisterThread();
    }

    void initialPopulate() {
        TURF_ASSERT(m_addIndex == m_removeIndex);
        MapAdapter::Map* map = m_shared->map;
        for (ureg i = 0; i < m_shared->numKeysPerThread; i++) {
            u32 key = m_addIndex * Prime;
            map->assign(key, (void*) (key & ~uptr(3)));
            if (++m_addIndex == m_rangeHi)
                m_addIndex = m_rangeLo;
        }
    }

    void run() {
        MapAdapter::Map* map = m_shared->map;
        turf::CPUTimer::Converter converter;
        Delay delay(m_shared->delayFactor);
        Stats stats;
        ureg lookupIndex = m_rangeLo;
        ureg remaining = m_shared->itersPerChunk;
        if (m_threadIndex == 0)
            m_shared->spinKicker.kick(m_shared->numThreads - 1);
        else {
            remaining = ~u32(0);
            m_shared->spinKicker.waitForKick();
        }

        // ---------
        turf::CPUTimer::Point start = turf::CPUTimer::get();
        for (; remaining > 0; remaining--) {
            // Add
            delay.delay(stats.workUnitsDone);
            if (m_shared->doneFlag.load(turf::Relaxed))
                break;
            u32 key = m_addIndex * Prime;
            if (key >= 2) {
                map->assign(key, (void*) uptr(key));
                stats.mapOpsDone++;
            }
            if (++m_addIndex == m_rangeHi)
                m_addIndex = m_rangeLo;

            // Lookup
            if (s32(lookupIndex - m_removeIndex) < 0)
                lookupIndex = m_removeIndex;
            for (ureg l = 0; l < m_shared->readsPerWrite; l++) {
                delay.delay(stats.workUnitsDone);
                if (m_shared->doneFlag.load(turf::Relaxed))
                    break;
                key = lookupIndex * Prime;
                if (key >= 2) {
                    volatile void* value = map->get(key);
                    TURF_UNUSED(value);
                    stats.mapOpsDone++;
                }
                if (++lookupIndex == m_rangeHi)
                    lookupIndex = m_rangeLo;
                if (lookupIndex == m_addIndex)
                    lookupIndex = m_removeIndex;
            }

            // Remove
            delay.delay(stats.workUnitsDone);
            if (m_shared->doneFlag.load(turf::Relaxed))
                break;
            key = m_removeIndex * Prime;
            if (key >= 2) {
                map->erase(key);
                stats.mapOpsDone++;
            }
            if (++m_removeIndex == m_rangeHi)
                m_removeIndex = m_rangeLo;

            // Lookup
            if (s32(lookupIndex - m_removeIndex) < 0)
                lookupIndex = m_removeIndex;
            for (ureg l = 0; l < m_shared->readsPerWrite; l++) {
                delay.delay(stats.workUnitsDone);
                if (m_shared->doneFlag.load(turf::Relaxed))
                    break;
                key = lookupIndex * Prime;
                if (key >= 2) {
                    volatile void* value = map->get(key);
                    TURF_UNUSED(value);
                    stats.mapOpsDone++;
                }
                if (++lookupIndex == m_rangeHi)
                    lookupIndex = m_rangeLo;
                if (lookupIndex == m_addIndex)
                    lookupIndex = m_removeIndex;
            }
        }
        if (m_threadIndex == 0)
            m_shared->doneFlag.store(1, turf::Relaxed);
        m_threadCtx.update();
        turf::CPUTimer::Point end = turf::CPUTimer::get();
        // ---------

        stats.duration = converter.toSeconds(end - start);
        m_stats = stats;
    }
};

static const turf::extra::Option Options[] = {
    {"readsPerWrite", 'r', true, "number of reads per write"},
    {"itersPerChunk", 'i', true, "number of iterations per chunk"},
    {"chunks", 'c', true, "number of chunks to execute"},
    {"keepChunkFraction", 'k', true, "threshold fraction of chunk timings to keep"},
};

int main(int argc, const char** argv) {
    turf::extra::Options options(Options, TURF_STATIC_ARRAY_SIZE(Options));
    options.parse(argc, argv);
    ureg readsPerWrite = options.getInteger("readsPerWrite", DefaultReadsPerWrite);
    ureg itersPerChunk = options.getInteger("itersPerChunk", DefaultItersPerChunk);
    ureg chunks = options.getInteger("chunks", DefaultChunks);
    double keepChunkFraction = options.getDouble("keepChunkFraction", 1.0);

    turf::extra::JobDispatcher dispatcher;
    ureg numThreads = dispatcher.getNumPhysicalCores();
    MapAdapter adapter(numThreads);

    // Create shared state and register threads
    SharedState shared(adapter, numThreads, NumKeysPerThread, readsPerWrite, itersPerChunk);
    std::vector<ThreadState> threads;
    threads.reserve(numThreads);
    for (ureg t = 0; t < numThreads; t++) {
        u32 rangeLo = 0xffffffffu / numThreads * t + 1;
        u32 rangeHi = 0xffffffffu / numThreads * (t + 1) + 1;
        threads.push_back(ThreadState(&shared, t, rangeLo, rangeHi));
    }
    dispatcher.kickMulti(&ThreadState::registerThread, &threads[0], threads.size());

    {
        // Create the map
        MapAdapter::Map map(MapAdapter::getInitialCapacity(numThreads * NumKeysPerThread));
        shared.map = &map;
        dispatcher.kickMulti(&ThreadState::initialPopulate, &threads[0], threads.size());

        printf("{\n");
        printf("'mapType': '%s',\n", MapAdapter::getMapName());
        printf("'readsPerWrite': %d,\n", (int) readsPerWrite);
        printf("'itersPerChunk': %d,\n", (int) itersPerChunk);
        printf("'chunks': %d,\n", (int) chunks);
        printf("'keepChunkFraction': %f,\n", keepChunkFraction);
        printf("'labels': ('delayFactor', 'workUnitsDone', 'mapOpsDone', 'totalTime'),\n"), printf("'points': [\n");
        for (float delayFactor = 1.f; delayFactor >= 0.0005f; delayFactor *= 0.95f) {
            shared.delayFactor = delayFactor;

            std::vector<ThreadState::Stats> kickTotals;
            for (ureg c = 0; c < chunks; c++) {
                shared.doneFlag.storeNonatomic(false);
                dispatcher.kickMulti(&ThreadState::run, &threads[0], threads.size());

                ThreadState::Stats kickTotal;
                for (ureg t = 0; t < numThreads; t++)
                    kickTotal += threads[t].m_stats;
                kickTotals.push_back(kickTotal);
            }

            std::sort(kickTotals.begin(), kickTotals.end());
            ThreadState::Stats totals;
            for (ureg t = 0; t < ureg(kickTotals.size() * keepChunkFraction); t++) {
                totals += kickTotals[t];
            }

            printf("    (%f, %d, %d, %f),\n", shared.delayFactor, int(totals.workUnitsDone), int(totals.mapOpsDone),
                   totals.duration);
        }
        printf("],\n");
        printf("}\n");

        shared.map = NULL;
    }

    dispatcher.kickMulti(&ThreadState::unregisterThread, &threads[0], threads.size());
    return 0;
}
