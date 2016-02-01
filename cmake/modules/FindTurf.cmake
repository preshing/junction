#----------------------------------------------
# When Turf is found, it's basically as if TurfProjectDefs.cmake was included.
#
# Later, this might find binary packages too.
#
# Sets:
#    TURF_FOUND
#    TURF_ROOT - the root folder
#    TURF_INCLUDE_DIRS - pass to include_directories()
#    AddTurfTarget() - call this to actually add the target
#
# You'll want to set the compiler options before calling AddTurfTarget().
#----------------------------------------------

find_path(TURF_ROOT NAMES "CMakeLists.txt" "cmake/TurfProjectDefs.cmake" PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/../turf"
    "${CMAKE_SOURCE_DIR}/../turf"
    "${CMAKE_CURRENT_LIST_DIR}/../../../turf")

if(TURF_ROOT)
    include("${TURF_ROOT}/cmake/TurfProjectDefs.cmake")
else()
    message("Can't find Turf!")
    if(Turf_FIND_REQUIRED)
        message(FATAL_ERROR "Missing required package Turf")
    endif()
endif()
