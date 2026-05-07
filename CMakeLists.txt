# Find all required vcpkg packages
find_package(SDL3 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Vulkan REQUIRED)

# Create Spherical library
add_library(Spherical STATIC
        src/SPHERICAL.cpp
        include/SPHERICAL.h
)

# Set include directories for internal headers
target_include_directories(Spherical PUBLIC include)

# Link external dependencies
target_link_libraries(Spherical PUBLIC
        SDL3::SDL3-shared
        glm::glm
        Vulkan::Vulkan
)