cmake_minimum_required(VERSION 3.24)

project(SPHERICAL VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(SPHERICAL_BUILD_SHARED "Build SPHERICAL as a shared library" ON)
option(SPHERICAL_COMPILE_SHADERS "Compile GLSL shaders to SPIR-V" ON)

# Find required packages (typically resolved through vcpkg toolchain).
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Freetype REQUIRED)
find_package(msdfgen CONFIG REQUIRED)

set(SPHERICAL_SHADER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui_nuklear.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui_nuklear_ui.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui_nuklear_grayscale.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui_nuklear_msdf.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui_triangle.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui_triangle.frag
)

set(SPHERICAL_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(SPHERICAL_SHADER_OUTPUTS)

if(SPHERICAL_COMPILE_SHADERS)
    if(NOT Vulkan_GLSLC_EXECUTABLE)
        message(FATAL_ERROR "SPHERICAL_COMPILE_SHADERS=ON but Vulkan_GLSLC_EXECUTABLE was not found.")
    endif()

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
endif()

set(SPHERICAL_LIBRARY_TYPE STATIC)
if(SPHERICAL_BUILD_SHARED)
    set(SPHERICAL_LIBRARY_TYPE SHARED)
endif()

add_library(Spherical ${SPHERICAL_LIBRARY_TYPE}
        src/NuklearImpl.cpp
        src/SPHERICAL.cpp
        src/VulkanRenderer.cpp
        src/FontRenderer.cpp
        src/TaskRunner.cpp
)

add_library(SPHERICAL::SPHERICAL ALIAS Spherical)
set_target_properties(Spherical PROPERTIES EXPORT_NAME SPHERICAL)

if(SPHERICAL_COMPILE_SHADERS)
    add_dependencies(Spherical SphericalShaders)
endif()

target_include_directories(Spherical PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/Nuklear>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(Spherical PRIVATE src)  # For nuklear_config.h

target_compile_definitions(Spherical PRIVATE
        SPHERICAL_EXPORTS
        SPHERICAL_SHADER_DIR="${SPHERICAL_SHADER_OUTPUT_DIR}"
)

target_compile_options(Spherical PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/FS>
)

set_property(TARGET Spherical PROPERTY CXX_STANDARD 17)
set_property(TARGET Spherical PROPERTY CXX_STANDARD_REQUIRED ON)

# Link external dependencies
target_link_libraries(Spherical PUBLIC
        SDL3::SDL3
        glm::glm
        Vulkan::Vulkan
        Freetype::Freetype
        msdfgen::msdfgen
        msdfgen::msdfgen-ext
)

install(TARGETS Spherical
        EXPORT SPHERICALTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Nuklear/nuklear.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(SPHERICAL_COMPILE_SHADERS)
    install(FILES ${SPHERICAL_SHADER_OUTPUTS}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/spherical/shaders
    )
else()
    install(FILES ${SPHERICAL_SHADER_SOURCES}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/spherical/shaders
    )
endif()

write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/SPHERICALConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SPHERICALConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/SPHERICALConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SPHERICAL
)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/SPHERICALConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/SPHERICALConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SPHERICAL
)

install(EXPORT SPHERICALTargets
        FILE SPHERICALTargets.cmake
        NAMESPACE SPHERICAL::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SPHERICAL
)

