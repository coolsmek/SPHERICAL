find_package(SDL3 CONFIG REQUIRED)

add_executable(SPHERICAL_Test main.cpp)
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
)

# Windows-specific: Use Windows subsystem for GUI app
if (MSVC)
    set_target_properties(SPHERICAL_Test PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/SPHERICAL-TEST/Debug"
    )
endif()
