include("${TURF_ROOT}/cmake/Macros.cmake")

find_path(NBDS_ROOT NAMES
          include/hashtable.h
          include/map.h
          include/rcu.h
          include/common.h
          PATHS
          ~/nbds)
if(NBDS_ROOT)
    set(NBDS_FOUND TRUE)
    set(NBDS_INCLUDE_DIRS ${NBDS_ROOT}/include)
    GetFilesWithSourceGroups(GLOB_RECURSE NBDS_FILES ${NBDS_ROOT} ${NBDS_ROOT}/include/*
        ${NBDS_ROOT}/runtime/runtime.c
        ${NBDS_ROOT}/runtime/rcu.c
        ${NBDS_ROOT}/runtime/lwt.c
        ${NBDS_ROOT}/runtime/random.c
        ${NBDS_ROOT}/datatype/nstring.c
        ${NBDS_ROOT}/runtime/hazard.c
        ${NBDS_ROOT}/map/map.c
        ${NBDS_ROOT}/map/list.c
        ${NBDS_ROOT}/map/skiplist.c
        ${NBDS_ROOT}/map/hashtable.c)
    if(NBDS_USE_TURF_HEAP)
        add_definitions(-DUSE_SYSTEM_MALLOC=1)
    else()
        GetFilesWithSourceGroups(GLOB NBDS_FILES ${NBDS_ROOT} ${NBDS_ROOT}/runtime/mem.c)
    endif()
    # FIXME: This is hacky because it relies on inheriting the same include_directories() as Junction
    add_library(NBDS ${NBDS_FILES})
    set(NBDS_LIBRARIES NBDS)
else()
    message("Can't find NBDS!")
    if(NBDS_FIND_REQUIRED)
        message(FATAL_ERROR "Missing required package NBDS")
    endif()
endif()
