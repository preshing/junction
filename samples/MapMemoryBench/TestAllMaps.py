#!/usr/bin/env python
import os
import subprocess
import sys

# Default CMake command is just 'cmake' but you can override it by setting
# the CMAKE environment variable:
CMAKE = os.getenv('CMAKE', 'cmake')

ALL_MAPS = [
    ('michael', 'junction/extra/impl/MapAdapter_CDS_Michael.h', ['-DJUNCTION_WITH_CDS=1', '-DTURF_WITH_EXCEPTIONS=1']),
    ('linear', 'junction/extra/impl/MapAdapter_Linear.h', []),
    ('leapfrog', 'junction/extra/impl/MapAdapter_Leapfrog.h', []),
    ('grampa', 'junction/extra/impl/MapAdapter_Grampa.h', []),
    ('stdmap', 'junction/extra/impl/MapAdapter_StdMap.h', []),
    ('folly', 'junction/extra/impl/MapAdapter_Folly.h', ['-DJUNCTION_WITH_FOLLY=1', '-DTURF_WITH_EXCEPTIONS=1']),
    ('nbds', 'junction/extra/impl/MapAdapter_NBDS.h', ['-DJUNCTION_WITH_NBDS=1']),
    ('tbb', 'junction/extra/impl/MapAdapter_TBB.h', ['-DJUNCTION_WITH_TBB=1']),
    ('tervel', 'junction/extra/impl/MapAdapter_Tervel.h', ['-DJUNCTION_WITH_TERVEL=1']),
]

# Scan arguments for path to CMakeLists.txt and args to pass through.
passThroughArgs = []
pathArgs = []
for arg in sys.argv[1:]:
    if arg.startswith('-'):
        passThroughArgs.append(arg)
    else:
        pathArgs.append(arg)
if len(pathArgs) != 1:
    sys.stderr.write('You must provide exactly one path argument.\n')
    exit(1)

listFilePath = os.path.abspath(pathArgs[0])
for suffix, include, cmakeOpts in ALL_MAPS:
    subdir = "build-%s" % suffix
    if not os.path.exists(subdir):
        os.mkdir(subdir)
    os.chdir(subdir)
    print("Configuring in %s..." % subdir)
    with open('junction_userconfig.h.in', 'w') as f:
        f.write('#define JUNCTION_IMPL_MAPADAPTER_PATH "%s"\n' % include)

    userConfigCMakePath = os.path.abspath('junction_userconfig.h.in')
    if os.sep != '/':
        userConfigCMakePath = userConfigCMakePath.replace(os.sep, '/')
    if subprocess.call([CMAKE, listFilePath, '-DCMAKE_INSTALL_PREFIX=TestAllMapsInstallFolder', '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DTURF_USE_DLMALLOC=1', '-DTURF_DLMALLOC_FAST_STATS=1',
                       '-DJUNCTION_USERCONFIG=%s' % userConfigCMakePath] + passThroughArgs + cmakeOpts) == 0:
        subprocess.check_call([CMAKE, '--build', '.', '--target', 'install', '--config', 'RelWithDebInfo'])
        print('Running in %s...' % subdir)
        results = subprocess.check_output([os.path.join('TestAllMapsInstallFolder', 'bin', 'MapMemoryBench')])
        with open('results.txt', 'wb') as f:
            f.write(results)
    os.chdir("..")
