include("${TURF_ROOT}/cmake/Macros.cmake")

find_path(TERVEL_ROOT NAMES tervel/containers/wf/hash-map/wf_hash_map.h PATHS
          ~/tervel-library)
if(TERVEL_ROOT)
    set(TERVEL_FOUND TRUE)
    set(TERVEL_INCLUDE_DIRS ${TERVEL_ROOT})
    GetFilesWithSourceGroups(GLOB_RECURSE TERVEL_FILES ${TERVEL_ROOT} ${TERVEL_ROOT}/tervel/containers/* ${TERVEL_ROOT}/tervel/util/*)
    # FIXME: This is hacky because it relies on inheriting the same include_directories() as Junction
    add_library(Tervel ${TERVEL_FILES})
    set(TERVEL_LIBRARIES Tervel)
else()
    message("Can't find Tervel!")
    if(Tervel_FIND_REQUIRED)
        message(FATAL_ERROR "Missing required package Tervel")
    endif()
endif()
