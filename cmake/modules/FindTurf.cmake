#----------------------------------------------
# This find module locates the Turf *source*.
#
# Later, it might find binary packages too.
#
# Sets:
#    TURF_FOUND
#    TURF_ROOT - the root folder, containing CMakeLists.txt
#    TURF_INCLUDE_DIRS - pass to include_directories()
#----------------------------------------------

if(TURF_ROOT)
    get_filename_component(fullPath "${TURF_ROOT}" ABSOLUTE)
    if(EXISTS "${fullPath}/cmake/turf_config.h.in")
        set(TURF_FOUND TRUE)
    endif()
else()
    find_path(TURF_ROOT "cmake/turf_config.h.in" PATHS
        "${CMAKE_CURRENT_SOURCE_DIR}/../turf"
        "${CMAKE_SOURCE_DIR}/../turf"
        "${CMAKE_CURRENT_LIST_DIR}/../../../turf")
    if(TURF_ROOT)
        set(TURF_FOUND TRUE)
    endif()
endif()

if(NOT TURF_FOUND)
    message("Can't find Turf!")
    if(Turf_FIND_REQUIRED)
        message(FATAL_ERROR "Missing required package Turf")
    endif()
endif()
