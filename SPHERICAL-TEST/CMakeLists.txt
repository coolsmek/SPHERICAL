find_package(SDL3 CONFIG REQUIRED)

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

add_custom_command(TARGET SPHERICAL_Test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SPHERICAL_Test>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
            "$<TARGET_FILE_DIR:SPHERICAL_Test>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Spherical>"
            "$<TARGET_FILE_DIR:SPHERICAL_Test>/"
)

# Windows-specific: Use Windows subsystem for GUI app
if (MSVC)
    set_target_properties(SPHERICAL_Test PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/SPHERICAL-TEST/Debug"
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