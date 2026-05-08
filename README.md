# SPHERICAL

**SPHERICAL** is a C++ GUI SDK prototype focused on building fast, responsive, desktop-style tooling UIs on top of **Vulkan 1.4 dynamic rendering**.

---

### If you want to use SPHERICAL and build from source, here are some additional notes:

#### Getting Nuklear (Submodule)

#### Dependencies

This project uses the [Nuklear](https://github.com/immediate-mode-ui/nuklear) immediate-mode GUI library, which is included as a Git submodule located in `SPHERICAL-SDK/third_party/Nuklear`.

### Cloning for the first time
To clone this repository along with the Nuklear submodule, use the `--recursive` flag:

```
git clone --recursive [https://github.com/coolsmek/SPHERICAL.git](https://github.com/coolsmek/SPHERICAL.git)
```
#### ALSO:
- SPHERICAL uses vcpkg to manage its dependencies. See vcpkg.json for more info.
- SPHERICAL uses CMake to manage its build.
- this repo includes font Roboto-VariableFont_wdth,wght.ttf for use with the demo app. see SPHEREICAL-TEST/fonts/OFL.txt for license details.

---

At its core, the project combines:

- **Vulkan** for explicit GPU control and low-latency presentation
- **SDL3** for windowing, platform integration, and input events
- **Nuklear** for immediate-mode UI construction
- **FreeType** for baked font atlas generation and text rendering

The current milestone is an interactive demo app that proves the end-to-end pipeline: window creation, Vulkan initialization, Nuklear UI generation, font atlas upload, command conversion, and on-screen rendering of an actual control panel.

---

## Vision

SPHERICAL is aiming at more than “just another widget library.”

The long-term goal is a **tooling-oriented GUI SDK** for applications that care about:

- **very low input latency**
- **stable frame pacing**
- **predictable rendering**
- **high-quality text**
- **desktop-tool ergonomics**

The project is intentionally shaped around the needs of editor-style software:

- control panels
- command palettes
- high-frequency interaction
- responsive sliders / buttons / text fields
- eventual background task orchestration without blocking the UI loop

In short: SPHERICAL is trying to be a foundation for **native-feeling, GPU-driven desktop tools** rather than a game HUD or a web wrapper.

---

## Project Goals

### Near-term goals

- Render a working immediate-mode GUI through Vulkan dynamic rendering
- Support text, panels, sliders, buttons, and text input cleanly
- Keep UI interaction responsive within the same rendered frame
- Provide a simple SDK-facing interface through `Spherical::Init`, `NewFrame`, `Render`, and `Shutdown`

### Medium-term goals

- Add a **task runner** for non-blocking background work
- Add a **command palette** (`Ctrl+P`-style workflow)
- Improve interaction polish, including **hover fade animation**
- Tighten swapchain/runtime behavior and validation robustness

### Longer-term goals

- Upgrade bitmap text to **SDF / MSDF-quality** rendering
- Improve high-DPI behavior and scaling quality
- Expand the SDK from a working prototype into a more reusable editor/toolkit foundation

---

## Current Status

SPHERICAL is currently in a **fully functional prototype** stage with all core rendering paths complete and runtime validated.

### ✅ Phases 1–3: Complete

- **Phase 1B**: Vulkan dynamic rendering pipeline (`vkCmdBeginRendering`) fully implemented
- **Phase 2 (Base Atlas)**: FreeType font atlas baking and rendering working end-to-end
- **Phase 2B**: FreeType SDF text rendering implemented; minor thin-stroke polish remains
- **Phase 3**: SDL3 input mapping to Nuklear complete (mouse, keyboard, text input all interactive)

### Demo App Live & Functional

The demo renders an interactive control panel with:
  - ✅ FPS label (60-frame rolling average)
  - ✅ Mouse position tracking
  - ✅ RGB sliders (zero-latency responsiveness)
  - ✅ Clickable button with counter
  - ✅ Text input field with echo display
  - ✅ All text rendered with the FreeType SDF atlas

### Next Steps

Phase 4 (Task Runner) is implemented in the SDK and wired into the demo flow; Phase 5 (Command Palette + Hover Animation) remains the next major feature block.

For detailed implementation status:

- `dev-docs/SPHERICAL_SCOPE.md` — Full architecture, phase tracking, and next priorities
- `dev-docs/demoApp_ControlPanel.md` — Demo app specification and runtime checklist

---

## Tech Stack

### Language / Build

- **C++**
- **CMake**
- **vcpkg** for dependency management

### Rendering / Windowing / UI

- **Vulkan 1.4**
  - dynamic rendering
  - explicit swapchain / synchronization management
- **SDL3**
  - native window creation
  - Vulkan surface integration
  - input/event collection
- **Nuklear**
  - immediate-mode UI authoring
  - draw command generation
- **FreeType**
  - glyph rasterization
  - atlas generation for UI text

### Additional libraries

- **GLM**

---

## Why Vulkan + Nuklear?

This project is intentionally opinionated.

### Why Vulkan?

Because the SDK wants explicit control over:

- presentation behavior
- synchronization
- dynamic rendering setup
- GPU buffer ownership
- low-latency frame submission

That makes it a good fit for tooling UIs where interaction quality matters and hidden abstraction cost is undesirable.

### Why Nuklear?

Because Nuklear provides:

- a compact immediate-mode UI model
- a small, understandable surface area
- direct access to draw commands and vertex buffer output
- a workflow that maps well onto a custom renderer

SPHERICAL treats Nuklear as the **UI authoring layer**, while Vulkan remains the **rendering execution layer**.

---

## Architecture Overview

The project is currently organized around a small set of focused modules.

### `SPHERICAL-SDK`

The SDK layer exposes the main lifecycle entry points:

- `Spherical::Init(...)`
- `Spherical::NewFrame()`
- `Spherical::SetRenderTarget(...)`
- `Spherical::Render(...)`
- `Spherical::Shutdown()`

This is the public-facing integration surface the demo app uses.

### `VulkanRenderer`

Responsible for:

- dynamic rendering setup
- graphics pipeline creation
- descriptor set / sampler setup for UI textures
- UI vertex/index buffer upload targets
- command recording helpers for draw submission

Relevant files:

- `SPHERICAL-SDK/include/VulkanRenderer.h`
- `SPHERICAL-SDK/src/VulkanRenderer.cpp`

### `FontRenderer`

Responsible for:

- font loading through FreeType
- glyph rasterization
- atlas baking
- atlas upload to Vulkan
- `nk_user_font` callbacks for width and glyph query

Relevant files:

- `SPHERICAL-SDK/include/FontRenderer.h`
- `SPHERICAL-SDK/src/FontRenderer.cpp`

### `SPHERICAL-TEST`

The standalone demo application that validates the SDK end to end.

It owns:

- SDL window creation
- Vulkan instance / device / swapchain / sync objects
- the frame loop
- per-frame render target selection

Relevant file:

- `SPHERICAL-TEST/main.cpp`

---

## Render Flow

At a high level, each frame works like this:

1. Acquire a swapchain image
2. Update the active render target for SPHERICAL
3. Poll and forward input events into Nuklear
4. Build the UI for the current frame
5. Convert Nuklear commands into vertex/index buffers
6. Begin Vulkan dynamic rendering
7. Draw each Nuklear command with scissoring
8. End rendering and present

Conceptually:

```cpp
while (running) {
	acquire_next_swapchain_image();
	Spherical::SetRenderTarget(...);
	Spherical::NewFrame();
	Spherical::Render(cmd);
	submit_and_present();
}
```

---

## Demo App

The current demo exists to prove the SDK is not just initializing, but actually usable.

### What it demonstrates

- Vulkan window-to-screen rendering
- Nuklear panel rendering
- live text rendering through the baked atlas
- interactive controls
- real-time UI state updates

### Current UI elements

- Control Panel window
- FPS display
- mouse position display
- RGB sliders
- live color readout
- button + click counter
- text input field
- echoed captured string

This demo is the current reference implementation for how the SDK is expected to be integrated.

---

## Repository Layout

```text
SPHERICAL/
├─ SPHERICAL-SDK/
│  ├─ include/
│  ├─ shaders/
│  └─ src/
│  └─ third_party/Nuklear    <-- submodule https://github.com/immediate-mode-ui/nuklear
├─ SPHERICAL-TEST/
│  ├─ fonts/
│  └─ main.cpp
├─ dev-docs/
├─ cmake/
├─ CMakeLists.txt
└─ build_all.bat
```

---

## Building

### Requirements

You will need:

- a C++ toolchain supported by CMake
- Vulkan SDK / loader available through your environment or package setup
- `vcpkg` dependencies available for this project
- CMake on `PATH`

### Recommended build (default: build only)

```powershell
.\build_all.bat Debug cmake-build-spherical_debug
```

### Build and run the demo explicitly

```powershell
.\build_all.bat Debug cmake-build-spherical_debug --run
```

### Manual CMake build

```powershell
cmake -S . -B .\cmake-build-spherical_debug
cmake --build .\cmake-build-spherical_debug --config Debug --target SPHERICAL_Test
```

### Run the demo directly

```powershell
.\cmake-build-spherical_debug\SPHERICAL-TEST\Debug\SPHERICAL_Test.exe
```

---

## Runtime Notes

- The demo bundles its font assets into the runtime output folder.
- The current text path uses a FreeType SDF atlas.
- Explorer / double-click launch is supported by resolving font assets relative to the executable output.

---

## Roadmap

### Phase 1 — Vulkan Dynamic Rendering Pipeline
- [x] dynamic rendering scaffold
- [x] Nuklear draw command submission through Vulkan

### Phase 2 — Font Texture + Real `nk_user_font`
- [x] ASCII glyph baking
- [x] Vulkan atlas upload
- [x] `nk_user_font` callbacks for width/glyph query
- [x] SDF text rendering path
- [ ] MSDF text rendering / further polish

### Phase 3 — SDL3 → Nuklear Input Mapping
- [x] mouse / key / wheel / text event forwarding
- [x] host-vs-SDK event ownership semantics validated

### Phase 4 — Task Runner (Implemented in SDK)
- [x] background work queue (`std::thread` + mutex/condition_variable)
- [x] main-thread completion polling
- [x] non-blocking "loading" workflows

### Phase 5 — Command Palette + Hover Animation (Next Priority)
- [ ] command palette popup (`Ctrl+P` triggered)
- [ ] fuzzy search command registry
- [ ] interaction polish / hover fades

### Phase 2 Full — SDF Text Rendering (Polish Enhancement)
- [x] Upgrade bitmap atlas to SDF (Signed Distance Fields)
- [ ] Scale-independent/MSDF polish for smaller glyphs
- [ ] High-DPI support refinement

---

## Lofty Goals

SPHERICAL is explicitly aspiring to become the kind of GUI SDK that feels appropriate for:

- editor tooling
- technical applications
- custom content tools
- control surfaces for real-time systems

That means not just “draw widgets,” but eventually:

- **crisp scalable text**
- **stable high refresh UI loops**
- **non-blocking background tasks**
- **command-driven workflows**
- **more refined interaction feel**

The ambition is to push toward the perceived quality of professional desktop tools while staying native, explicit, and understandable.

---

## Documentation

Additional project notes live in:

- `SPHERICAL-SDK/include/docs/nuklear_map.md`

The Nuklear map was generated specifically to make the large single-header library easier to reason about during development.

---

## Contributing / Development Notes

This project is still evolving rapidly, so the internal structure may change while the SDK surface settles.

If you are working in the repo, it helps to think in layers:

1. **host app layer** — SDL window, Vulkan objects, swapchain, frame loop
2. **SDK layer** — lifecycle + UI orchestration
3. **renderer layer** — Vulkan pipeline + draw submission
4. **font layer** — FreeType baking + texture upload + glyph metadata

When debugging UI rendering issues, that separation is often the fastest way to localize the problem.

---

## Status Summary

SPHERICAL is now a **fully functional prototype** demonstrating all core rendering capabilities:

- ✅ **Vulkan-backed immediate-mode GUI** rendering at low latency
- ✅ **Real interactive controls** (sliders, buttons, text fields)
- ✅ **Custom renderer ownership** with explicit GPU pacing
- ✅ **Text rendering** with FreeType SDF-baked glyph atlas
- ✅ **Input integration** with SDL3 and Nuklear

**What's working now:** The demo app builds, runs, and renders an interactive control panel with visible text, responsive sliders, clickable buttons, and text input. All basic rendering, input, font, and background task paths are validated end-to-end.

**What's next:** Command palette for workflow, hover animations for polish, and optional MSDF refinement for even better scale-independence.

