cmake_minimum_required(VERSION 3.28) # Required for modern Vulkan/vcpkg support
project(SPHERICAL LANGUAGES CXX)

# Force CMake to find the 1.4 components
find_package(Vulkan 1.4 REQUIRED)

add_subdirectory(SPHERICAL-SDK)
add_subdirectory(SPHERICAL-TEST)