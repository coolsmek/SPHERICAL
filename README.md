# SPHERICAL SDK

Lightweight Vulkan UI SDK for embedding SPHERICAL into your CMake projects.

> Status: Pre-release v0.2.1 API and packaging may change between minor versions.

## Contents

- [Overview](#overview)
- [Repository Layout](#repository-layout)
- [Requirements](#requirements)
- [Build Options](#build-options)
- [Quick Start (Subdirectory)](#quick-start-subdirectory)
- [Install + find_package](#install--find_package)
- [vcpkg Toolchain Setup](#vcpkg-toolchain-setup)
- [Shaders (Runtime Assets)](#shaders-runtime-assets)
- [Nuklear Dependency](#nuklear-dependency)
- [Versioning and Releases](#versioning-and-releases)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## Overview

`SPHERICAL-SDK` ships as a CMake-based SDK with:

- Public headers in `include/`
- Implementation in `src/` (single library target, shared by default)
- Runtime GLSL shaders in `shaders/` (compiled to SPIR-V during build by default)
- Embedded or submodule dependency in `third_party/Nuklear/`

## Repository Layout

```text
SPHERICAL-SDK/
  CMakeLists.txt
  include/
  src/
  shaders/
  cmake/
  third_party/Nuklear/
  vcpkg.json
  README.md
  LICENSE
```

## Requirements

- CMake 3.24+ (adjust to actual minimum used by this repo)
- C++17 compiler
- Vulkan SDK tools available (for `glslc` shader compilation when enabled)
- vcpkg (manifest mode via `SPHERICAL-SDK/vcpkg.json`)

## Build Options

- `SPHERICAL_BUILD_SHARED` (default: `ON`): build shared library (`.dll`/`.so`)
- `SPHERICAL_COMPILE_SHADERS` (default: `ON`): compile `.vert`/`.frag` to `.spv`

## Quick Start (Subdirectory)

Add this SDK repo under your project, then include it with CMake:

```cmake
# Your project's CMakeLists.txt
add_subdirectory(external/SPHERICAL-SDK)
target_link_libraries(your_app PRIVATE SPHERICAL::SPHERICAL)
```

Configure your app with vcpkg toolchain so dependencies resolve from manifests:

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="<vcpkg-root>/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_BUILD_TYPE=Release
```

## Install + find_package

Build and install the SDK first:

```powershell
cmake -S . -B build `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="<vcpkg-root>/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
cmake --install build --config Release --prefix "C:/sdk/spherical"
```

Then consume from another project:

```cmake
# Consumer CMakeLists.txt
find_package(SPHERICAL CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE SPHERICAL::SPHERICAL)
```

If needed, point CMake to the install prefix:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/sdk/spherical"
```

## vcpkg Toolchain Setup

This SDK includes a local `vcpkg.json`. In manifest mode, vcpkg will install SDK dependencies automatically.

Configure with the vcpkg toolchain:

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="<vcpkg-root>/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_BUILD_TYPE=Release
```

Notes:

- Use a fresh build directory when changing toolchain or manifest settings.
- Set `-DVCPKG_TARGET_TRIPLET=...` if you need a non-default triplet.
- Keep the vcpkg triplet consistent between SDK and consuming app.

## Shaders (Runtime Assets)

Shader files in `shaders/` are source assets. By default, the build compiles them to `.spv`.

Installed shader output path:

```text
<install-prefix>/share/spherical/shaders/
  ui_nuklear.vert.spv
  ui_nuklear_ui.frag.spv
  ui_nuklear_msdf.frag.spv
  ui_nuklear_grayscale.frag.spv
  ui_triangle.vert.spv
  ui_triangle.frag.spv
```

Recommended layout:

```text
<app-binary-dir>/
  your_app.exe
  shaders/
    ui_nuklear.vert.spv
    ui_nuklear_ui.frag.spv
    ui_nuklear_msdf.frag.spv
    ui_nuklear_grayscale.frag.spv
    ui_triangle.vert.spv
    ui_triangle.frag.spv
```

At integration time, do one of the following:

- Copy built `.spv` shaders from your SDK build output next to your executable.
- Install shaders to a known path and configure the SDK to load from that path.
- Provide a runtime override path via app configuration/env var (if supported by your SDK build).

## Nuklear Dependency

If `third_party/Nuklear/` is a submodule, clone recursively:

```powershell
git clone --recursive <repo-url>
```

If already cloned without submodules:

```powershell
git submodule update --init --recursive
```

## Versioning and Releases

- Use semantic pre-release tags while API is evolving (for example, `v0.1.14-alpha`).
- Publish release notes with API changes, shader changes, and breaking build changes.
- Keep this README focused on consumer-facing setup only.

## Troubleshooting

- **Missing shaders at runtime**: verify compiled `.spv` files are copied to runtime output.
- **Package not found**: verify `CMAKE_PREFIX_PATH` includes SDK install prefix.
- **Dependency mismatch**: ensure SDK and app use the same vcpkg triplet/toolchain.
- **Submodule files missing**: run `git submodule update --init --recursive`.

## License

See `LICENSE` for SDK licensing terms.
Third-party license details are in `third_party/Nuklear/LICENSE`.

