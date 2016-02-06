cmake_minimum_required(VERSION 2.8.5)

get_filename_component(SAMPLE_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # CMAKE_CONFIGURATION_TYPES only reliable if set before project(), and not from an include file
    set(CMAKE_CONFIGURATION_TYPES "Debug;RelWithAsserts;RelWithDebInfo" CACHE INTERNAL "Build configs")
    project(${SAMPLE_NAME})
endif()    

set(TEST_CHECK_MAP_CONTENTS TRUE CACHE BOOL "Validate contents of the map using its iterator")
set(JUNCTION_USERCONFIG "junction_userconfig.h.in" CACHE STRING "Custom config for ${SAMPLE_NAME}")
include(../AddSample.cmake)
