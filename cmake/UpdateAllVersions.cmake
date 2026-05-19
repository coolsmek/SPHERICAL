# Master script to update all version-dependent files
# Usage: cmake -DPROJECT_SOURCE_DIR=<root> -P cmake/UpdateAllVersions.cmake

if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR must be defined")
endif()

# Resolve to absolute path
get_filename_component(PROJECT_SOURCE_DIR "${PROJECT_SOURCE_DIR}" ABSOLUTE)

# Read version from cmake/version.cmake
include(${PROJECT_SOURCE_DIR}/cmake/version.cmake)

set(VERSION_STRING "${SPHERICAL_VERSION}-${SPHERICAL_VERSION_SUFFIX}")

# Update README files
execute_process(
        COMMAND ${CMAKE_COMMAND}
        -DREADME_FILE=${PROJECT_SOURCE_DIR}/README.md
        -DVERSION_STRING=${VERSION_STRING}
        -P ${PROJECT_SOURCE_DIR}/cmake/UpdateVersionInReadme.cmake
)

execute_process(
        COMMAND ${CMAKE_COMMAND}
        -DREADME_FILE=${PROJECT_SOURCE_DIR}/SPHERICAL-SDK/README.md
        -DVERSION_STRING=${VERSION_STRING}
        -P ${PROJECT_SOURCE_DIR}/cmake/UpdateVersionInReadme.cmake
)

message(STATUS "All version numbers updated to ${VERSION_STRING}")