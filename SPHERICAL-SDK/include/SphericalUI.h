#pragma once

/**
 * @file SphericalUI.h
 * @brief Vulkan/Nuklear-free UI widget API for Spherical SDK
 * 
 * Applications use this API to define UI panels and widgets.
 * The SDK handles translation to Nuklear and Vulkan internally.
 */

#include <functional>

#if defined(_WIN32)
#  if defined(SPHERICAL_EXPORTS)
#    define SPHERICAL_API __declspec(dllexport)
#  else
#    define SPHERICAL_API __declspec(dllimport)
#  endif
#else
#  define SPHERICAL_API
#endif



namespace Spherical {
    
    struct UIColor {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct UIVec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct UIRect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    enum class ButtonStyle {
        Flat,
        Embossed
    };

    /**
     * @enum DockLayout
     * @brief Defines the split direction for panels docked in a workspace container.
     */
    enum class DockLayout {
        SideBySide,  ///< Panels arranged horizontally (left-right)
        TopBottom    ///< Panels arranged vertically (top-bottom)
    };
    
    /**
     * @enum FontRenderMode
     * @brief Defines the glyph rasterization strategy used by the SDK font system.
     */
    enum class FontRenderMode {
        Grayscale, ///< Hinted grayscale rendering for sharp small UI text.
        MSDF       ///< Multi-channel Signed Distance Field rendering for sharper corners across scales.
    };

    /**
     * @enum FontStyle
     * @brief Defines standard UI font styles for text rendering.
     */
    enum class FontStyle {
        Small,   ///< Smaller text for dense UI elements (Grayscale: FT_LOAD_TARGET_NORMAL, MSDF: no hinting)
        Regular, ///< Default body text (Grayscale: FT_LOAD_TARGET_NORMAL, MSDF: no hinting)
        Title    ///< Larger text for titles and headers (Grayscale: FT_LOAD_TARGET_LIGHT, MSDF: no hinting)
    };

    /**
     * @enum PanelFlags
     * @brief Additional window behaviors for standard panels.
     */
    enum class PanelFlags : uint32_t {
        None = 0,
        NoScrollbar = 1 << 0,
        NoTitle = 1 << 1,
        NoPadding = 1 << 2,
        Locked = 1 << 3,
        NoUndock = 1 << 4,
        AutoMaximize = 1 << 5
    };
    
    inline PanelFlags operator|(PanelFlags a, PanelFlags b) {
        return static_cast<PanelFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline bool operator&(PanelFlags a, PanelFlags b) {
        return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
    }

    struct WorkspaceContainerStyle {
        bool solidBorder = false;
        float borderThickness = 1.0f;
        UIColor borderColor = {145.0f/255.0f, 170.0f/255.0f, 205.0f/255.0f, 1.0f};
    };

    /**
     * @class UIPainter
     * @brief Immediate-mode UI painter for defining widgets
     * 
     * Applications call methods on a UIPainter each frame to define
     * the UI layout. The SDK internally translates to Nuklear commands.
     */
    class UIPainter {
    public:
        virtual ~UIPainter() = default;

        /**
         * @brief Begin a new panel (window)
         * @param title Panel title bar text
         * @param x X position in pixels
         * @param y Y position in pixels
         * @param width Panel width in pixels
         * @param height Panel height in pixels
         * @return true if panel is expanded and content should be drawn
         * @note end_panel() MUST always be called after begin_panel(), regardless of return value.
         *       If false is returned (panel is collapsed), skip content but still call end_panel().
         *
         * Correct usage:
         * @code
         *   if (ui.begin_panel("Title", 10, 10, 300, 400)) {
         *       ui.label("Content");
         *   }
         *   ui.end_panel();  // Always call this
         * @endcode
         */
        virtual bool begin_panel(const char* title, int x, int y, int width, int height, PanelFlags flags = PanelFlags::None) = 0;

        /**
         * @brief End the current panel
         * @note Must always be called after begin_panel(), even if begin_panel() returned false.
         */
        virtual void end_panel() = 0;

        /**
         * @brief Begin a menu bar fixed to the top of the main window.
         * @return true if the menu bar is expanded and content should be drawn.
         * @note end_menu_bar() MUST always be called after begin_menu_bar().
         */
        virtual bool begin_menu_bar() = 0;

        /**
         * @brief End the current menu bar.
         * @note Must always be called after begin_menu_bar().
         */
        virtual void end_menu_bar() = 0;

        /**
         * @brief Begin a dropdown menu from the menu bar.
         * @param label The text shown on the menu bar button.
         * @return true if the dropdown is currently open and items should be drawn.
         */
        virtual bool begin_dropdown_menu(const char* label) = 0;

        /**
         * @brief End the current dropdown menu.
         * @note Must always be called after begin_dropdown_menu(), even if it returned false.
         */
        virtual void end_dropdown_menu() = 0;

        /**
         * @brief Add a selectable item to a dropdown menu.
         * @param label The text for the menu item.
         * @return true if the item was clicked this frame.
         */
        virtual bool menu_item(const char* label) = 0;


        /**
         * @brief Begin a collapsible subsection inside the current panel
         * @param title Subsection header text
         * @return true if the subsection is expanded and content should be drawn
         * @note end_panel_subsection() MUST always be called after begin_panel_subsection(),
         *       regardless of return value. If false is returned (subsection is collapsed),
         *       skip content but still call end_panel_subsection().
         *
         * Subsections default to expanded the first time they appear and remember their
         * expansion state across frames. Nested subsections are supported and each nested
         * subsection maintains its own state using its full parent/child path.
         *
         * Correct usage:
         * @code
         *   if (ui.begin_panel_subsection("Display")) {
         *       ui.label("Visible only while expanded");
         *   }
         *   ui.end_panel_subsection();  // Always call this
         * @endcode
         */
        virtual bool begin_panel_subsection(const char* title) = 0;

        /**
         * @brief End the current panel subsection
         * @note Must always be called after begin_panel_subsection(), even if it returned false.
         */
        virtual void end_panel_subsection() = 0;

        /**
         * @brief Add a static label
         * @param text Label text
         */
        virtual void label(const char* text) = 0;

        /**
         * @brief Pushes a specific font style onto the style stack for subsequent widgets.
         * @param style The font style to use.
         * @note Must be balanced with a call to pop_font().
         */
        virtual void push_font(FontStyle style) = 0;

        /**
         * @brief Pops the last pushed font style from the style stack.
         */
        virtual void pop_font() = 0;

        /**
         * @brief Add a horizontal spacer
         */
        virtual void spacing() = 0;

        /**
         * @brief Add a floating-point slider in a 2-column layout
         * @param label Left column label
         * @param value Pointer to the float variable to modify
         * @param min Minimum slider value
         * @param max Maximum slider value
         * @param step Slider step size
         */
        virtual void slider_float(const char* label, float* value, float min, float max, float step) = 0;

        /**
         * @brief Add a button
         * @param label Button text
         * @return true if clicked this frame
         */
        virtual bool button(const char* label) = 0;

        /**
         * @brief Add a text input field
         * @param label Label text
         * @param buffer Character buffer for input text
         * @param bufferSize Size of the buffer
         */
        virtual void text_input(const char* label, char* buffer, size_t bufferSize) = 0;

        /**
         * @brief Get the current framebuffer width in pixels
         */
        virtual uint32_t get_framebuffer_width() const = 0;

        /**
         * @brief Get the current framebuffer height in pixels
         */
        virtual uint32_t get_framebuffer_height() const = 0;
        
        /**
         * @brief Add a radio button (part of an integer-indexed group)
         * @param label Label text shown next to the radio control
         * @param activeIndex Pointer to the integer that holds the currently selected value for the group
         * @param value The integer value that corresponds to this radio option
         * @return true if the active value changed as a result of user interaction
         */
        virtual bool radio_button(const char* label, int* activeIndex, int value) = 0;
        
        /**
         * @brief Add an image widget using a custom texture handle
         * @param texture The handle returned from VulkanRenderer::RegisterTexture
         * @param width Width in pixels
         * @param height Height in pixels
         */
        virtual void image(void* texture_handle_ptr, float width, float height) = 0;
        
        /**
         * @brief Add an image widget exactly centered in the available space.
         * @param texture The handle returned from VulkanRenderer::RegisterTexture
         * @param width Width in pixels
         * @param height Height in pixels
         * @param available_width The total available content width
         * @param available_height The total available content height
         */
        virtual void image_centered(void* texture_handle_ptr, float width, float height, float available_width, float available_height) = 0;
        
        
        /**
         * @brief Push a color override for regular panel content text.
         * @note Must be balanced with pop_text_color().
         */
        virtual void push_text_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last regular text color override.
         */
        virtual void pop_text_color() = 0;

        /**
         * @brief Push a panel body color override.
         * @note Internally applies to both Nuklear window.background and window.fixed_background.
         *       Must be balanced with pop_panel_body_color().
         */
        virtual void push_panel_body_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last panel body color override.
         */
        virtual void pop_panel_body_color() = 0;

        /**
         * @brief Push a panel title bar color override.
         * @note Applies to the title bar normal/hover/active header background states.
         *       Must be balanced with pop_panel_title_bar_color().
         */
        virtual void push_panel_title_bar_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last panel title bar color override.
         */
        virtual void pop_panel_title_bar_color() = 0;

        /**
         * @brief Push a panel border color override.
         * @note Must be balanced with pop_panel_border_color().
         */
        virtual void push_panel_border_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last panel border color override.
         */
        virtual void pop_panel_border_color() = 0;

        /**
         * @brief Push a panel title text color override.
         * @note Applies to the title text normal/hover/active header label states.
         *       Must be balanced with pop_panel_title_text_color().
         */
        virtual void push_panel_title_text_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last panel title text color override.
         */
        virtual void pop_panel_title_text_color() = 0;

        /**
         * @brief Push a panel title font style override.
         * @note Must be balanced with pop_panel_title_font().
         */
        virtual void push_panel_title_font(FontStyle style) = 0;

        /**
         * @brief Pop the last panel title font style override.
         */
        virtual void pop_panel_title_font() = 0;

        /**
         * @brief Push a panel title bar padding override.
         * @note Controls the extra space added around the title text, effectively changing title bar height.
         *       Must be balanced with pop_panel_title_padding().
         */
        virtual void push_panel_title_padding(const UIVec2& padding) = 0;

        /**
         * @brief Pop the last panel title bar padding override.
         */
        virtual void pop_panel_title_padding() = 0;

        /**
         * @brief Push a radio button label text color override.
         * @note Applies to the normal/hover/active radio label text states.
         *       Must be balanced with pop_radio_button_text_color().
         */
        virtual void push_radio_button_text_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last radio button label text color override.
         */
        virtual void pop_radio_button_text_color() = 0;

        /**
         * @brief Push a regular button background color override.
         * @note Applies to the normal/hover/active button background states.
         *       Must be balanced with pop_button_background_color().
         */
        virtual void push_button_background_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last regular button background color override.
         */
        virtual void pop_button_background_color() = 0;

        /**
         * @brief Push a regular button hover background color override.
         * @note Applies to the button hover background state only.
         *       Must be balanced with pop_button_hover_background_color().
         */
        virtual void push_button_hover_background_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last regular button hover background color override.
         */
        virtual void pop_button_hover_background_color() = 0;

        /**
         * @brief Push a regular button clicked background color override.
         * @note Applies to the button active/clicked background state only.
         *       Must be balanced with pop_button_clicked_background_color().
         */
        virtual void push_button_clicked_background_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last regular button clicked background color override.
         */
        virtual void pop_button_clicked_background_color() = 0;

        /**
         * @brief Push a button height override in pixels.
         * @note Must be balanced with pop_button_height().
         */
        virtual void push_button_height(float height) = 0;

        /**
         * @brief Pop the last button height override.
         */
        virtual void pop_button_height() = 0;

        /**
         * @brief Push a button width override in pixels.
         * @note The button remains centered within its parent panel row.
         *       Values <= 0 fall back to full available row width.
         *       Must be balanced with pop_button_width().
         */
        virtual void push_button_width(float width) = 0;

        /**
         * @brief Pop the last button width override.
         */
        virtual void pop_button_width() = 0;

        /**
         * @brief Push a button corner radius override in pixels.
         * @note Must be balanced with pop_button_corner_radius().
         */
        virtual void push_button_corner_radius(float radius) = 0;

        /**
         * @brief Pop the last button corner radius override.
         */
        virtual void pop_button_corner_radius() = 0;

        /**
         * @brief Push a button border thickness override in pixels.
         * @note Must be balanced with pop_button_border_thickness().
         */
        virtual void push_button_border_thickness(float thickness) = 0;

        /**
         * @brief Pop the last button border thickness override.
         */
        virtual void pop_button_border_thickness() = 0;

        /**
         * @brief Push a button content padding override in pixels.
         * @note Must be balanced with pop_button_padding().
         */
        virtual void push_button_padding(const UIVec2& padding) = 0;

        /**
         * @brief Pop the last button content padding override.
         */
        virtual void pop_button_padding() = 0;

        /**
         * @brief Push a button rendering style override.
         * @note Use ButtonStyle::Embossed for a beveled XP-style appearance.
         *       Must be balanced with pop_button_style().
         */
        virtual void push_button_style(ButtonStyle style) = 0;

        /**
         * @brief Pop the last button rendering style override.
         */
        virtual void pop_button_style() = 0;

        /**
         * @brief Push a button bevel highlight color override.
         * @note Used by embossed buttons for the top/left edge.
         *       Must be balanced with pop_button_highlight_color().
         */
        virtual void push_button_highlight_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last button bevel highlight color override.
         */
        virtual void pop_button_highlight_color() = 0;

        /**
         * @brief Push a button bevel shadow color override.
         * @note Used by embossed buttons for the bottom/right edge.
         *       Must be balanced with pop_button_shadow_color().
         */
        virtual void push_button_shadow_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last button bevel shadow color override.
         */
        virtual void pop_button_shadow_color() = 0;

        /**
         * @brief Push a regular button border color override.
         * @note Must be balanced with pop_button_border_color().
         */
        virtual void push_button_border_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last regular button border color override.
         */
        virtual void pop_button_border_color() = 0;

        /**
         * @brief Push a regular button text color override.
         * @note Applies to the normal/hover/active button text states.
         *       Must be balanced with pop_button_text_color().
         */
        virtual void push_button_text_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last regular button text color override.
         */
        virtual void pop_button_text_color() = 0;

        /**
         * @brief Push a text input box background color override.
         * @note Applies to the normal/hover/active text input background states.
         *       Must be balanced with pop_text_input_background_color().
         */
        virtual void push_text_input_background_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last text input box background color override.
         */
        virtual void pop_text_input_background_color() = 0;

        /**
         * @brief Push a text input typing text color override.
         * @note Applies to the editable text states, selected text states, and cursor-over-text states.
         *       Must be balanced with pop_text_input_text_color().
         */
        virtual void push_text_input_text_color(const UIColor& color) = 0;

        /**
         * @brief Pop the last text input typing text color override.
         */
        virtual void pop_text_input_text_color() = 0;

        /**
         * @brief Get the current panel bounds for the active panel.
         * @note Valid between a successful begin_panel() and the matching end_panel().
         */
        virtual UIRect get_current_panel_bounds() const = 0;

        /**
         * @brief Get the current panel content bounds for the active panel.
         * @return Bounding rectangle of the inner content area (ignoring borders and title bar)
         */
        virtual UIRect get_current_panel_content_bounds() const = 0;

        /**
         * @brief Check if the current panel is actively being moved or resized by the user.
         * @return true if the panel is currently being dragged or resized.
         * @note Must be called between begin_panel() and end_panel().
         */
        virtual bool is_current_panel_resizing() const = 0;

        /**
         * @brief Begin a workspace container (hierarchical panel docking host)
         * @param title Container title bar text
         * @param x X position in pixels
         * @param y Y position in pixels
         * @param width Container width in pixels
         * @param height Container height in pixels
         * @return true if container is expanded and content should be rendered
         * @note end_workspace_container() MUST always be called after begin_workspace_container(),
         *       regardless of return value. If false is returned (container is collapsed),
         *       skip content but still call end_workspace_container().
         *
         * Correct usage:
         * @code
         *   if (ui.begin_workspace_container("Workspace", 50, 50, 800, 600)) {
         *       // Render panels that will be docked here
         *   }
         *   ui.end_workspace_container();  // Always call this
         * @endcode
         */
        virtual bool begin_workspace_container(const char* title, int x, int y, int width, int height, PanelFlags flags = PanelFlags::None, const WorkspaceContainerStyle* style = nullptr) = 0;

        /**
         * @brief End the current workspace container
         * @note Must always be called after begin_workspace_container(), even if it returned false.
         */
        virtual void end_workspace_container() = 0;

        /**
         * @brief Get the number of panels docked in a workspace container
         * @param containerTitle Title of the workspace container
         * @return Number of docked panels (leaf nodes in the BSP tree), or 0 if container not found
         */
        virtual int get_workspace_panel_count(const char* containerTitle) const = 0;

        /**
         * @brief Get the top-level dock layout of a workspace container
         * @param containerTitle Title of the workspace container
         * @return DockLayout of the root split node if it exists, otherwise SideBySide by default
         */
        virtual DockLayout get_workspace_dock_layout(const char* containerTitle) const = 0;

        /**
         * @brief Undock a panel from its workspace container, restoring it to free-floating
         * @param panelTitle Title of the panel to undock
         * @note No-op if the panel is not currently docked. Internally collapses any resulting
         *       single-child split nodes and reflows the BSP tree.
         */
        virtual void undock_panel_from_workspace(const char* panelTitle) = 0;
    };

    /**
     * @typedef UIBuildFn
     * @brief Callback type for UI building
     * 
     * Applications implement this callback to define UI each frame.
     * Signature: void(UIPainter& painter)
     */
    using UIBuildFn = std::function<void(UIPainter&)>;

    /**
     * @brief Register a UI build callback
     * 
     * The provided callback will be called each frame during Spherical::Render()
     * to allow the app to define the UI layout and widgets.
     * 
     * @param callback Function or lambda that builds the UI
     * 
     * Example:
     * @code
     *   Spherical::RegisterUI([](Spherical::UIPainter& ui) {
     *       if (ui.begin_panel("My Panel", 20, 20, 300, 400)) {
     *           ui.label("Hello, World!");
     *           if (ui.button("Click Me")) {
     *               // Button was clicked
     *           }
     *       }
     *       ui.end_panel();  // Always call end_panel()
     *   });
     * @endcode
     */
    SPHERICAL_API void RegisterUI(const UIBuildFn& callback);

} // namespace Spherical





