#pragma once

/**
 * @file SphericalUI.h
 * @brief Vulkan/Nuklear-free UI widget API for Spherical SDK
 * 
 * Applications use this API to define UI panels and widgets.
 * The SDK handles translation to Nuklear and Vulkan internally.
 */

#include <functional>

namespace Spherical {

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
        Regular, ///< Default body text (Grayscale: FT_LOAD_TARGET_NORMAL, MSDF: no hinting)
        Title    ///< Larger text for titles and headers (Grayscale: FT_LOAD_TARGET_LIGHT, MSDF: no hinting)
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
        virtual bool begin_panel(const char* title, int x, int y, int width, int height) = 0;

        /**
         * @brief End the current panel
         * @note Must always be called after begin_panel(), even if begin_panel() returned false.
         */
        virtual void end_panel() = 0;

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
         *
         * Usage:
         *   // single int stored by the application represents the selected value
         *   if (ui.radio_button("Option A", &selectedIndex, 0)) {
         *       // selection changed to value 0
         *   }
         */
        virtual bool radio_button(const char* label, int* activeIndex, int value) = 0;
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
    void RegisterUI(const UIBuildFn& callback);

} // namespace Spherical





