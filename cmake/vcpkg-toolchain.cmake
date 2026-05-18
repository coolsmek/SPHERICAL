if(NOT DEFINED VCPKG_ROOT AND DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE PATH "Vcpkg root directory")
endif()

if(NOT DEFINED VCPKG_ROOT)
    set(VCPKG_ROOT "C:/dev/vcpkg" CACHE PATH "Vcpkg root directory")
endif()

include("${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

