#---------------------------------------------------------------------------
# This script is included from the CMakeLists.txt (listfile) of sample applications.
#---------------------------------------------------------------------------

# Were we included from the root listfile?
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Yes, it's the root.
    get_filename_component(outerPath "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    set(TURF_ROOT "${outerPath}/turf" CACHE STRING "Path to Turf")
    include("${TURF_ROOT}/cmake/Macros.cmake")
    ApplyTurfBuildSettings()
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." junction)
endif()

# Define executable target.
set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.mycompany.\${PRODUCT_NAME:identifier}")
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
GetFilesWithSourceGroups(GLOB SAMPLE_FILES ${CMAKE_CURRENT_SOURCE_DIR} *)
add_executable(${SAMPLE_NAME} MACOSX_BUNDLE ${SAMPLE_FILES})
set_target_properties(${SAMPLE_NAME} PROPERTIES XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer")
set_target_properties(${SAMPLE_NAME} PROPERTIES FOLDER samples)
install(TARGETS ${SAMPLE_NAME} DESTINATION bin)

# Set include dirs and libraries
include_directories(${JUNCTION_ALL_INCLUDE_DIRS})
target_link_libraries(${SAMPLE_NAME} ${JUNCTION_ALL_LIBRARIES})
AddDLLCopyStep(${SAMPLE_NAME} ${JUNCTION_ALL_DLLS})
