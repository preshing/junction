# Add cmake/modules to module search path, so subsequent find_package() commands will work.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/modules")

# FIXME: Implement FindJunction.cmake that other projects can use
# If this script was invoked from FindJunction.cmake, JUNCTION_ROOT should already be set.
if(NOT DEFINED JUNCTION_ROOT)
    get_filename_component(JUNCTION_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
endif()

set(JUNCTION_FOUND TRUE)
set(JUNCTION_INCLUDE_DIRS ${JUNCTION_ROOT})

# Find Turf
if(NOT TURF_FOUND)
    set(TURF_WITH_EXCEPTIONS FALSE CACHE BOOL "Enable compiler support for C++ exceptions")
    find_package(Turf REQUIRED)
endif()
