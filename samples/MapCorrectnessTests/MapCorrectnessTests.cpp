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
#include "TestEnvironment.h"
#include "TestInsertSameKeys.h"
#include "TestInsertDifferentKeys.h"
#include "TestChurn.h"
#include <turf/extra/Options.h>
#include <junction/details/Grampa.h> // for GrampaStats

static const ureg IterationsPerLog = 100;

int main(int argc, const char** argv) {
    TestEnvironment env;

    TestInsertSameKeys testInsertSameKeys(env);
    TestInsertDifferentKeys testInsertDifferentKeys(env);
    TestChurn testChurn(env);
    for (;;) {
        for (ureg c = 0; c < IterationsPerLog; c++) {
            testInsertSameKeys.run();
            testInsertDifferentKeys.run();
            testChurn.run();
        }
        turf::Trace::Instance.dumpStats();

#if JUNCTION_TRACK_GRAMPA_STATS
        junction::DefaultQSBR.flush();
        junction::details::GrampaStats& stats = junction::details::GrampaStats::Instance;
        printf("---------------------------\n");
        printf("numTables: %d/%d\n", (int) stats.numTables.current.load(turf::Relaxed),
               (int) stats.numTables.total.load(turf::Relaxed));
        printf("numTableMigrations: %d/%d\n", (int) stats.numTableMigrations.current.load(turf::Relaxed),
               (int) stats.numTableMigrations.total.load(turf::Relaxed));
        printf("numFlatTrees: %d/%d\n", (int) stats.numFlatTrees.current.load(turf::Relaxed),
               (int) stats.numFlatTrees.total.load(turf::Relaxed));
        printf("numFlatTreeMigrations: %d/%d\n", (int) stats.numFlatTreeMigrations.current.load(turf::Relaxed),
               (int) stats.numFlatTreeMigrations.total.load(turf::Relaxed));
#endif
    }

    return 0;
}
