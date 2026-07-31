# Spherical SDK Changes

The following modifications were made to the SPHERICAL SDK to support integrating the X-Ray Engine Vulkan viewport:

* **Full-Color UI Textures (`ui_nuklear_ui.frag`)**:
  * Modified the core UI fragment shader to output `texel * inColor` instead of using the texture's red channel as an alpha mask. This allows full-RGB Vulkan framebuffers to render natively within Nuklear without being tinted grayscale.

* **Vulkan Context & Texture Exposure (`SPHERICAL.h`, `SPHERICAL.cpp`, `VulkanRenderer.h`, `VulkanRenderer.cpp`)**:
  * Exposed `Spherical::GetVulkanContext()` to allow host applications (like `XrayModelViewer`) to seamlessly retrieve the underlying Vulkan Instance, Physical Device, Logical Device, and Graphics Queue initialized by SDL3.
  * Added `Spherical::RegisterTexture(VkImageView, VkSampler)` to allow external renderer backends to inject their own Vulkan framebuffers directly into the Nuklear UI system.
  * Added `Spherical::FreeTexture(void* handle)` and `VulkanRenderer::FreeTexture(nk_handle)` to allow applications to release Vulkan descriptor sets.
  * Added the `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` flag to the internal UI descriptor pool during initialization so textures can be dynamically resized and replaced without leaking descriptor memory (fixing the "Texture is NULL" error).

* **Advanced Panel Layout Control (`SphericalUI.h`, `UIPainterPanel.inl`)**:
  * Introduced the `Spherical::PanelFlags` bitmask enum (`None`, `NoScrollbar`, `NoTitle`, `NoPadding`).
  * Updated `UIPainter::begin_panel` to accept the new flags parameter.
  * Updated the internal `nk_begin` logic to conditionally apply `NK_WINDOW_NO_SCROLLBAR` and omit `NK_WINDOW_TITLE`.
  * Implemented dynamic Nuklear style pushing/popping in `begin_panel`/`end_panel` to override `window.padding` to `0,0` when `PanelFlags::NoPadding` is requested, enabling true edge-to-edge content rendering.

* **Absolute Image Centering (`SphericalUI.h`, `SPHERICAL.cpp`)**:
  * Added `UIPainter::image_centered()` which utilizes Nuklear's `nk_layout_space_begin` and absolute positioning. This bypasses the default auto-layout row padding constraints, allowing an image to be mathematically centered within an exact pixel region.

* **Drag-Resize Caching (`SphericalUI.h`, `SPHERICAL.cpp`)**:
  * Added `UIPainter::is_current_panel_resizing()` to expose the internal `g_panelDrag` state. This allows applications to bypass expensive Vulkan framebuffer/swapchain recreations while a panel is actively being dragged/resized by the user, and instead utilize hardware texture scaling on the cached image until the drag finishes.

* **Absolute Workspace Boundaries (`SPHERICAL.h`, `SPHERICAL.cpp`, `UIPainterPanel.inl`)**:
  * Extended `SphericalInitInfo` with `workspaceBoundarySize` to optionally define an absolute layout constraint zone.
  * Added drawing logic to `DrawCenterMarkerOverlay` to visually render the boundary as a solid black bounding rectangle in the background.
  * Extensively upgraded the `clamp_bounds` algorithm inside `UIPainterPanel.inl` to strictly peg panels to this border. Added advanced `is_resizing` awareness to `clamp_bounds` so that moving a panel shifts the whole window to stay inside, while resizing a panel against the border strictly shrinks the actively-dragged edge without erroneously shifting the opposite edge.

* **Menu Bar and Native Dropdowns (`SphericalUI.h`, `UIPainterMenuBar.inl`, `UIPainterMenu.inl`)**:
  * Introduced fixed, top-level Menu Bar functionality via `begin_menu_bar()` and `end_menu_bar()`.
  * Implemented fully native Dropdown Menus (`begin_dropdown_menu()`, `menu_item()`) integrating directly into Nuklear's internal `nk_menubar_begin` layout system for authentic left-to-right stacking.
  * Dropdown panels intelligently and automatically size their width dynamically across frames based on the text bounds of their longest contained `menu_item`.
  * Applied flush, zero-padding geometry constraints and deep color theming to seamlessly merge dropdown buttons with the overarching menu bar aesthetic.

* **Panel Title Overrides (`SphericalUI.h`, `SPHERICAL.cpp`, `UIPainterPanel.inl`)**:
  * Exposed granular push/pop methods to customize Panel title bars: `push_panel_title_font` and `push_panel_title_padding`.
  * Allows applications to break away from the globally-defined `FontStyle::Title` font on a per-panel basis, while manipulating `nk_style_window_header_padding` to effortlessly squish or stretch the panel's top header height dynamically.

* **Workspace Container Extensions (`SphericalUI.h`, `DockState.h`, `UIPainterWorkspaceContainer.inl`)**:
  * Added `PanelFlags::Locked` enforcement. If a container is initialized as `Locked`, it bypasses historical persistent layout minimums and forcefully overrides bounds to exactly match the requested code dimensions, eliminating startup shrinkage.
  * Added `PanelFlags::NoUndock` and `DockState::disallowUndock` to completely lock docked nodes in place. Hides the "Undock All" toolbar button on containers and the individual undock buttons on panel title bars when active.

* **Cartesian Grid Coordinate System Refactor (`UIPainterPanel.inl`, `UIPainterWorkspaceContainer.inl`, `SPHERICAL.cpp`)**:
  * Completely overhauled `begin_panel` and `begin_workspace_container` initial coordinate parsing to function as a strict center-origin Cartesian grid.
  * The `x, y` parameters now dictate the exact **center** of the requested bounding box rather than the top-left edge.
  * `0,0` is permanently locked to the exact center of the internal window viewport.
  * Implemented mathematical inversion so that `+y` values shift geometry UPwards (Cartesian standard) instead of downwards (UI standard).
  * Synced `g_workspaceBoundaryOffset` to also natively respect this `-y` flip so the global constraint boundary perfectly mirrors the panel placement math.

* **Splitter Drag Awareness (`SPHERICAL.cpp`)**:
  * Significantly expanded the scope of `is_current_panel_resizing()` to intercept workspace modifications.
  * The method now internally traverses the `g_workspaceContainerStates` BSP layout trees to check if the active panel is docked inside a container whose internal splitter is currently being dragged (`g_splitterDrag.active`).
  * Seamlessly prevents host applications from prematurely reallocating expensive Vulkan resources (like viewport caching) during rapid docking layouts.

* **Razor-Sharp Docked Panel Borders (`UIPainterPanel.inl`, `SPHERICAL.cpp`)**:
  * Implemented an unclipped `nk_stroke_rect` overlay using `nk_push_scissor` to bypass Nuklear's internal content bounds clipping.
  * Explicitly bypasses anti-aliasing by pushing geometry directly onto half-pixel boundaries (+0.5f offset, -1.0f bounds), guaranteeing a perfectly crisp 1-pixel border for docked panels without layout shrinkage.

* **Auto-Maximizing Workspaces (`SphericalUI.h`, `UIPainterWorkspaceContainer.inl`)**:
  * Introduced `PanelFlags::AutoMaximize` to dynamically hook workspace containers directly into the native SDL/Vulkan window resolution via `m_framebufferExtent`.
  * The SDK automatically accommodates a fixed 26px top offset (for menu bars) and preserves a consistent 4px padding gutter around the entire viewport.
  * Bypasses standard `initialized` caching when enabled, ensuring that resizing the main application window perfectly flows and re-balances all docked BSP sub-panels every frame without user intervention.

* **High-Resolution Font Atlas Scaling (`FontRenderer.cpp`)**:
  * Introduced `kBakeResolutionScale` (default 2.0x) to strictly decouple the internal texture rendering size from the external logical UI layout size.
  * During the `Init()` baking phase, the SDK requests significantly larger glyphs from FreeType/MSDFGen to pack a higher-resolution texture atlas.
  * Internally scales down the logical `font.height` back to the exact requested point size, automatically triggering `NKGlyphQueryCallback` to downscale the `advanceWidth`, `height`, and `width` bounds by 0.5x while referencing the high-res texture coordinates.
  * Results in drastically sharper MSDF/Grayscale text rendering across the entire UI without breaking or bloating the physical size of buttons and layout containers.
