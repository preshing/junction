#FIXME: Deal with builds from different versions of Visual Studio
if(NOT CDS_ROOT)
    find_path(CDS_ROOT NAMES cds/init.h PATHS
        "${CMAKE_CURRENT_SOURCE_DIR}/../libcds"
        "${CMAKE_SOURCE_DIR}/../libcds"
        "${CMAKE_CURRENT_LIST_DIR}/../../../libcds"
        C:/Jeff/libcds-master)  # FIXME: Remove this one.
endif()

find_path(CDS_INCLUDE_DIR cds/init.h ${CDS_ROOT})
if(WIN32) #FIXME: CygWin
    find_library(CDS_LIBRARY libcds-x86-vcv140.lib "${CDS_ROOT}/bin/vc.v140/Win32")
    find_file(CDS_DLL libcds-x86-vcv140.dll "${CDS_ROOT}/bin/vc.v140/Win32")
else()
    find_library(CDS_LIBRARY cds "${CDS_ROOT}/bin/gcc-x86-linux-0" "${CDS_ROOT}/bin/gcc-amd64-linux-0")
endif()

if(CDS_LIBRARY AND CDS_INCLUDE_DIR)
    set(CDS_FOUND TRUE)
else()
    message("Can't find CDS!")
    if(CDS_FIND_REQUIRED)
        message(FATAL_ERROR "Missing required package CDS")
    endif()
endif()
