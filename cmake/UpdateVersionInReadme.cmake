# Script to update version numbers in README files using regex
# Usage: cmake -DREADME_FILE=<path> -DVERSION_STRING=<version> -P cmake/UpdateVersionInReadme.cmake

if(NOT DEFINED README_FILE OR NOT DEFINED VERSION_STRING)
    message(FATAL_ERROR "UpdateVersionInReadme.cmake requires README_FILE and VERSION_STRING variables")
endif()

if(NOT EXISTS "${README_FILE}")
    message(FATAL_ERROR "README file not found: ${README_FILE}")
endif()

file(READ "${README_FILE}" readme_content)

# Replace version pattern v0.x.x-alpha with new version
string(REGEX REPLACE "v[0-9]+\\.[0-9]+\\.[0-9]+-alpha" "v${VERSION_STRING}"
        readme_content "${readme_content}")

file(WRITE "${README_FILE}" "${readme_content}")
message(STATUS "Updated ${README_FILE} with version: v${VERSION_STRING}")