cmake_minimum_required(VERSION 3.28) # Required for modern Vulkan/vcpkg support

# Help IDE CMake profiles (e.g., Rider) resolve vcpkg packages when they do not
# explicitly pass -DCMAKE_TOOLCHAIN_FILE.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	set(_spherical_local_toolchain "${CMAKE_SOURCE_DIR}/cmake/vcpkg-toolchain.cmake")
	if(EXISTS "${_spherical_local_toolchain}")
		set(CMAKE_TOOLCHAIN_FILE "${_spherical_local_toolchain}" CACHE FILEPATH "Toolchain file" FORCE)
		message(STATUS "SPHERICAL: Auto-selected toolchain: ${CMAKE_TOOLCHAIN_FILE}")
	endif()
endif()

project(SPHERICAL VERSION 0.2.1 LANGUAGES CXX)
# Set the global C++ standard
set(CMAKE_CXX_STANDARD 23)           # Or 26 if you want to go bleeding edge
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # Error out if the compiler is too old
set(CMAKE_CXX_EXTENSIONS OFF)        # Use "Pure" C++ without compiler-specific junk
set (SPHERICAL_VERSION_SUFFIX "alpha")

# Force CMake to find the 1.4 components
find_package(Vulkan 1.4 REQUIRED)

add_subdirectory(SPHERICAL-SDK)
add_subdirectory(SPHERICAL-TEST)