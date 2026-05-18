find_package(SDL3 CONFIG REQUIRED)
include(GNUInstallDirs)

add_executable(SPHERICAL_Test
    main.cpp
    ui_theme.cpp
    ui_style_scope.cpp
)
target_include_directories(SPHERICAL_Test PRIVATE ${CMAKE_SOURCE_DIR}/SPHERICAL-SDK/include)
target_link_libraries(SPHERICAL_Test PRIVATE 
    Spherical 
    SDL3::SDL3
)

set(SPHERICAL_TEST_RUNTIME_DEP_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/CopyRuntimeDependencies.cmake")
file(GENERATE
    OUTPUT "${SPHERICAL_TEST_RUNTIME_DEP_SCRIPT}"
    CONTENT [=[
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

if(NOT WIN32)
    return()
endif()

foreach(required_var INPUT_EXECUTABLE INPUT_LIBRARY OUTPUT_DIR)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "CopyRuntimeDependencies.cmake missing required variable: ${required_var}")
    endif()
endforeach()

if(NOT DEFINED CLEAN_OUTPUT)
    set(CLEAN_OUTPUT ON)
endif()

set(_search_dirs)
foreach(candidate IN ITEMS "${SEARCH_DIR_1}" "${SEARCH_DIR_2}" "${OUTPUT_DIR}")
    if(candidate AND EXISTS "${candidate}")
        list(APPEND _search_dirs "${candidate}")
    endif()
endforeach()

if(CLEAN_OUTPUT)
    file(GLOB _staged_dlls "${OUTPUT_DIR}/*.dll")
    foreach(staged_dll IN LISTS _staged_dlls)
        file(REMOVE "${staged_dll}")
    endforeach()
endif()

file(GET_RUNTIME_DEPENDENCIES
    RESOLVED_DEPENDENCIES_VAR _resolved_deps
    UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
    EXECUTABLES "${INPUT_EXECUTABLE}"
    LIBRARIES "${INPUT_LIBRARY}"
    DIRECTORIES ${_search_dirs}
    PRE_EXCLUDE_REGEXES
        "api-ms-win-.*"
        "ext-ms-.*"
    POST_EXCLUDE_REGEXES
        ".*/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss]ystem32/.*\\.dll"
)

list(REMOVE_DUPLICATES _resolved_deps)

foreach(dep IN LISTS _resolved_deps)
    get_filename_component(dep_name "${dep}" NAME)
    file(COPY_FILE "${dep}" "${OUTPUT_DIR}/${dep_name}" ONLY_IF_DIFFERENT)
endforeach()

if(_unresolved_deps)
    list(REMOVE_DUPLICATES _unresolved_deps)
    message(WARNING "Unresolved runtime dependencies for ${INPUT_EXECUTABLE}: ${_unresolved_deps}")
endif()
]=]
)

add_custom_command(TARGET SPHERICAL_Test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SPHERICAL_Test>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
            "$<TARGET_FILE_DIR:SPHERICAL_Test>/fonts"
    COMMAND ${CMAKE_COMMAND}
            -DINPUT_EXECUTABLE="$<TARGET_FILE:SPHERICAL_Test>"
            -DINPUT_LIBRARY="$<TARGET_FILE:Spherical>"
            -DOUTPUT_DIR="$<TARGET_FILE_DIR:SPHERICAL_Test>"
            -DSEARCH_DIR_2="$<TARGET_FILE_DIR:Spherical>"
            -P "${SPHERICAL_TEST_RUNTIME_DEP_SCRIPT}"
)

install(TARGETS SPHERICAL_Test
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/fonts/"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/fonts"
)

set(_spherical_test_install_code [=[
set(_installed_bindir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/@CMAKE_INSTALL_BINDIR@")
file(GLOB _spherical_test_runtime_dlls "@CMAKE_BINARY_DIR@/${CMAKE_INSTALL_CONFIG_NAME}/bin/*.dll")

if(_spherical_test_runtime_dlls)
    file(INSTALL
        DESTINATION "${_installed_bindir}"
        TYPE SHARED_LIBRARY
        FILES ${_spherical_test_runtime_dlls}
    )
endif()
]=])
string(CONFIGURE "${_spherical_test_install_code}" _spherical_test_install_code @ONLY)
install(CODE "${_spherical_test_install_code}")

# Windows-specific: Use Windows subsystem for GUI app
if (MSVC)
    set_target_properties(SPHERICAL_Test PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:SPHERICAL_Test>"
    )
endif()

set(VER_FILEVERSION "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
set(VER_STRING "${PROJECT_VERSION}-${SPHERICAL_VERSION_SUFFIX}")

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/version.rc
        @ONLY
)

if (WIN32)
    target_sources(SPHERICAL_Test PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
endif()

target_compile_definitions(SPHERICAL_Test PRIVATE
        SPHERICAL_APP_VERSION="${PROJECT_VERSION}-${SPHERICAL_VERSION_SUFFIX}"
)