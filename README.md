<h1 align="center">SPHERICAL</h1>
<h3 align="center">-- A GUI LIBRARY / SDK --</h3>

<p align="center">
  <a href="https://www.youtube.com/watch?v=grtEt0Cudt8">
    <img src="images/SPHERICAL_header.jpg" alt="SPHERICAL">
  </a>
</p>

**SPHERICAL** is a C++ GUI SDK prototype focused on building fast, responsive, desktop-style tooling UIs on top of **Vulkan 1.4 dynamic rendering**.
> **Current Release** (Test Control Panel APP Only, **No SDK Library release yet**): [SPHERICAL_Test_PreRelease_v0.1.12-alpha.zip](https://github.com/coolsmek/SPHERICAL/releases/tag/SPHERICAL_Test)

---

--- 

Dev screenshots:
<p align="center">
    <img src="images/screenshots/screenshot-0001.jpg" alt="screenshot-001">
</p>

---

---

### If you want to use SPHERICAL and build from source, here are some additional notes:

#### Getting Nuklear (Submodule)

#### Dependencies

This project uses the [Nuklear](https://github.com/immediate-mode-ui/nuklear) immediate-mode GUI library, which is included as a Git submodule located in `SPHERICAL-SDK/third_party/Nuklear`.

### Cloning for the first time
To clone this repository along with the Nuklear submodule, use the `--recursive` flag:

```
git clone --recursive https://github.com/coolsmek/SPHERICAL.git
```
#### ALSO:
- SPHERICAL uses vcpkg to manage its dependencies. See vcpkg.json for more info.
- SPHERICAL uses CMake to manage its build.
- this repo includes font Arimo-Regular.ttf for use with the SPHERICAL_Test app. see SPHEREICAL-TEST/fonts/LICENSE.txt for license details.

---

At its core, the project combines:

- **Vulkan** for explicit GPU control and low-latency presentation
- **SDL3** for windowing, platform integration, and input events
- **Nuklear** for immediate-mode UI construction
- **FreeType** for baked font atlas generation and text rendering

The current milestone is an interactive demo app that proves the end-to-end pipeline: window creation, Vulkan initialization, Nuklear UI generation, font atlas upload, command conversion, and on-screen rendering of an actual control panel.

---

## Vision

SPHERICAL is aiming to be a functional GUI SDK for desktop applications, focusing on Game Development and Tooling 
environments integrating 3D viewport and user interaction. <- the latter is a stretch goal, but the former is the primary target.

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

### Application-Level vs. SDK-Level Separation

SPHERICAL enforces a **clean separation of concerns** between application code and graphics infrastructure:

#### Application Level (`SPHERICAL-TEST/main.cpp`)
- Defines **what UI should be displayed** (windows, controls, layout, state management)
- Uses **Vulkan-free, Nuklear-free API** via `Spherical::UIPainter`
- Implements a **UI build callback** registered with `Spherical::RegisterUI(callback)`
- Owns all **UI state** (slider values, text buffers, button flags, etc.)

Example:
```cpp
Spherical::RegisterUI([](Spherical::UIPainter& ui) {
    if (ui.begin_panel("My Panel", 20, 20, 300, 400)) {
        ui.label("Hello, World!");
        if (ui.button("Click Me")) { /* handle click */ }
        ui.slider_float("Value:", &myVar, 0.0f, 1.0f, 0.01f);
        ui.end_panel();
    }
});
```

#### SDK Level (`SPHERICAL-SDK`)
- Takes the **application's UI definitions** and translates them to **Nuklear + Vulkan**
- Manages **all Vulkan infrastructure** (instance, device, swapchain, command buffers, synchronization)
- Owns **immediate-mode UI rendering** and vertex buffer conversion
- Provides a clean lifecycle API: `Init()`, `NewFrame()`, `Render()`, `Shutdown()`

This separation means:
- ✅ **Apps never touch Vulkan or Nuklear** — they describe UI only
- ✅ **SDK owns graphics complexity** — implementation details are hidden
- ✅ **Future flexibility** — the SDK could swap Vulkan for DirectX/Metal/OpenGL without app changes
- ✅ **Reusability** — any app can use Spherical SDK for its UI, regardless of domain

### `SPHERICAL.h` Public API

The public lifecycle and UI entry points:

- `Spherical::Init(SphericalInitInfo)` — Initialize the SDK with a window
- `Spherical::NewFrame()` — Update input state from OS events
- `Spherical::Render()` — Render (SDK handles Vulkan acquire/record/submit/present internally)
- `Spherical::Shutdown()` — Cleanup
- **`Spherical::RegisterUI(UIBuildFn callback)`** — Register the app's UI definition callback
- `Spherical::UIPainter` — Abstract interface for UI authoring (labels, sliders, buttons, etc.)

### Using the Declarative UI API

Applications register a single UI build callback before calling `Spherical::Init()`. The callback is invoked every frame with a `UIPainter` object. Methods on the painter correspond to Nuklear widget calls, but apps never see Nuklear:

```cpp
Spherical::RegisterUI([](Spherical::UIPainter& ui) {
    if (ui.begin_panel("Control Panel", 20, 20, 350, 550)) {
        // Static labels
        ui.label("Status: Ready");
        
        // Interactive controls
        ui.slider_float("Intensity:", &intensity, 0.0f, 1.0f, 0.01f);
        
        // Buttons with immediate click detection
        if (ui.button("Apply")) {
            ApplySettings();
        }
        
        // Text input
        ui.text_input("Search:", searchBuffer, sizeof(searchBuffer));
        
        ui.end_panel();
    }
});

Spherical::Init(initInfo);
```

---

## Render Flow

### Current app-facing flow

1. Host app runs window/app loop
2. Host app calls `Spherical::NewFrame()`
3. Host app calls `Spherical::Render()`
4. SDK handles Vulkan acquire/record/submit/present for UI rendering

Conceptually:

```cpp
while (running) {
    Spherical::NewFrame();
    Spherical::Render();
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
├─ images/
├─ SPHERICAL-SDK/
│  ├─ include/
│  ├─ shaders/
│  ├─ src/
│  └─ third_party/Nuklear    <-- submodule https://github.com/immediate-mode-ui/nuklear
├─ SPHERICAL-TEST/
│  ├─ fonts/
│  └─ main.cpp
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