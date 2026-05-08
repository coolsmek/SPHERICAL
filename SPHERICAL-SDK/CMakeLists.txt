# Find all required vcpkg packages
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Freetype REQUIRED)

set(SPHERICAL_SHADER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui_nuklear.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui_nuklear.frag
)

set(SPHERICAL_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(SPHERICAL_SHADER_OUTPUTS)

foreach(SHADER_SOURCE ${SPHERICAL_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${SPHERICAL_SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SPHERICAL_SHADER_OUTPUT_DIR}
            COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE}
            VERBATIM
    )
    list(APPEND SPHERICAL_SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(SphericalShaders DEPENDS ${SPHERICAL_SHADER_OUTPUTS})

# Create Spherical library
add_library(Spherical STATIC
        src/NuklearImpl.cpp
        src/SPHERICAL.cpp
        src/VulkanRenderer.cpp
        src/FontRenderer.cpp
        src/TaskRunner.cpp
        include/SPHERICAL.h
)

add_dependencies(Spherical SphericalShaders)

# Set include directories for internal headers
target_include_directories(Spherical PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Nuklear
)
target_include_directories(Spherical PRIVATE src)  # For nuklear_config.h

target_compile_definitions(Spherical PRIVATE
        SPHERICAL_SHADER_DIR="${SPHERICAL_SHADER_OUTPUT_DIR}"
)

target_compile_options(Spherical PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/FS>
)

# Link external dependencies
target_link_libraries(Spherical PUBLIC
        SDL3::SDL3
        glm::glm
        Vulkan::Vulkan
        Freetype::Freetype
)
