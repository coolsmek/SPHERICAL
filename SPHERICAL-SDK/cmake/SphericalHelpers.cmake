# SphericalHelpers.cmake
# CMake helper functions for SPHERICAL SDK integration

# spherical_copy_runtime_shaders(target_name)
#
# Copies compiled SPIR-V shader files to the target's runtime directory.
# Requires SPHERICAL_COMPILE_SHADERS=ON and shaders to be built.
#
# Arguments:
#   target_name - The executable or library target that needs runtime shaders
#
# Creates structure:
#   $<TARGET_FILE_DIR:target_name>/shaders/*.spv
#
function(spherical_copy_runtime_shaders target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "spherical_copy_runtime_shaders: Target '${target_name}' does not exist")
    endif()

    if(NOT SPHERICAL_COMPILE_SHADERS)
        message(FATAL_ERROR "spherical_copy_runtime_shaders requires SPHERICAL_COMPILE_SHADERS=ON. "
                           "SPHERICAL requires compiled shaders - set SPHERICAL_COMPILE_SHADERS=ON or compile shaders externally.")
    endif()

    if(NOT DEFINED SPHERICAL_SHADER_OUTPUTS)
        message(FATAL_ERROR "spherical_copy_runtime_shaders: SPHERICAL_SHADER_OUTPUTS is not defined. "
                           "Ensure SphericalHelpers.cmake is included after shader compilation in SPHERICAL-SDK/CMakeLists.txt")
    endif()

    # Create shaders directory and copy all compiled shader files
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>/shaders"
        COMMENT "Creating shaders directory for ${target_name}"
    )

    foreach(shader_file ${SPHERICAL_SHADER_OUTPUTS})
        get_filename_component(shader_name "${shader_file}" NAME)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${shader_file}"
                    "$<TARGET_FILE_DIR:${target_name}>/shaders/${shader_name}"
            DEPENDS "${shader_file}"
            COMMENT "Copying shader: ${shader_name}"
        )
    endforeach()

    message(STATUS "Configured runtime shader copying for target: ${target_name}")
endfunction()

