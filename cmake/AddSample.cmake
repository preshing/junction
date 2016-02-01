#---------------------------------------------------------------------------
# This script is included from the CMakeLists.txt (listfile) of sample applications.
#---------------------------------------------------------------------------

if(NOT DEFINED PROJECT_NAME)
    message(FATAL_ERROR "project() should be called before including \"${CMAKE_CURRENT_LIST_FILE}\".")
endif()
if(NOT DEFINED SAMPLE_NAME)
    message(FATAL_ERROR "SAMPLE_NAME should be set before including \"${CMAKE_CURRENT_LIST_FILE}\".")
endif()

# Were we included from the root listfile?
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Yes, it's the root.
    include("${CMAKE_CURRENT_LIST_DIR}/JunctionProjectDefs.cmake")
    ApplyTurfBuildSettings()
    add_subdirectory(${JUNCTION_ROOT} junction)
elseif(NOT JUNCTION_FOUND)
    # No, it was added from a parent listfile (via add_subdirectory).
    # The parent is responsible for finding Junction before adding the sample.
    # (Or, Junction's listfile is the root, in which case Junction is already found.)
    message(FATAL_ERROR "JUNCTION_FOUND should already be set when \"${CMAKE_CURRENT_SOURCE_FILE}\" is not the root listfile.")
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
