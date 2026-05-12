#include "nuklear_config.h"
#include "SPHERICAL.h"
#include "VulkanRenderer.h"
#include "FontRenderer.h"
#include "TaskRunner.h"
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>
#include <array>
#include <cstddef>
#include <fstream>
#include <string>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
    struct BackendState {
        SDL_Window* window = nullptr;

        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        uint32_t graphicsQueueIndex = 0;
        VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkExtent2D swapchainExtent{1280, 720};

        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        std::vector<VkImageLayout> swapchainImageLayouts;

        uint32_t currentImageIndex = 0;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;

        bool preferImmediatePresent = true;
        bool initialized = false;
    };
    
    struct ScrollbarDragState {
        bool active = false;
        std::string windowTitle;
        float dragStartMouseY = 0.0f;
        float dragStartScrollY = 0.0f;
        float trackY = 0.0f;
        float trackH = 0.0f;
        float thumbH = 0.0f;
        float maxScrollY = 0.0f;
        float grabOffsetY = 0.0f;  // distance from mouse to top of thumb at click time
    };

    struct SliderTrackDragState {
        bool active = false;
        float* valueRef = nullptr;
    };
    
    BackendState g_backend;
    bool g_textInputWasActive = false;

    // UI registration and implementation
    Spherical::UIBuildFn g_uiBuildCallback = nullptr;

    float GetWindowDisplayScale(SDL_Window* window) {
        if (window == nullptr) {
            return 1.0f;
        }

        const float scale = SDL_GetWindowDisplayScale(window);
        return scale > 0.0f ? scale : 1.0f;
    }

    float ResolveUiScale(const Spherical::SphericalInitInfo& info, SDL_Window* window) {
        if (info.manualDpiScale > 0.0f) {
            return info.manualDpiScale;
        }
        return GetWindowDisplayScale(window);
    }

    bool IsNuklearTextEditActive(const nk_context* context) {
        if (context == nullptr || context->active == nullptr) {
            return false;
        }

        if (context->active->popup.win != nullptr) {
            return context->active->popup.win->edit.active != 0;
        }

        return context->active->edit.active != 0;
    }

    void SyncWindowTextInputState(const nk_context* context) {
        if (g_backend.window == nullptr || context == nullptr) {
            return;
        }

        const bool editActive = IsNuklearTextEditActive(context);
        if (editActive == g_textInputWasActive) {
            return;
        }

        const bool windowTextInputActive = SDL_TextInputActive(g_backend.window);
        if (editActive && !windowTextInputActive) {
            SDL_StartTextInput(g_backend.window);
        } else if (!editActive && windowTextInputActive) {
            SDL_StopTextInput(g_backend.window);
        }

        g_textInputWasActive = editActive;
    }

    ScrollbarDragState g_scrollbarDrag;
    SliderTrackDragState g_sliderTrackDrag;

    static bool IsLeftMouseDown(const nk_context* ctx) {
        return ctx != nullptr && ctx->input.mouse.buttons[NK_BUTTON_LEFT].down != 0;
    }

    static bool WasLeftMousePressed(const nk_context* ctx) {
        if (ctx == nullptr) {
            return false;
        }

        const nk_mouse_button& button = ctx->input.mouse.buttons[NK_BUTTON_LEFT];
        return button.down != 0 && button.clicked != 0;
    }

    static bool IsMouseInsideRect(const nk_context* ctx, const struct nk_rect& rect) {
        if (ctx == nullptr) {
            return false;
        }

        const float mx = ctx->input.mouse.pos.x;
        const float my = ctx->input.mouse.pos.y;
        return mx >= rect.x && mx <= (rect.x + rect.w) &&
               my >= rect.y && my <= (rect.y + rect.h);
    }
    
    class UIPainterImpl : public Spherical::UIPainter {
    private:
        struct PanelBodyStyleSnapshot {
            nk_color background;
            nk_style_item fixedBackground;
        };

        struct StyleItemStateSnapshot {
            nk_style_item normal;
            nk_style_item hover;
            nk_style_item active;
        };

        struct ColorStateSnapshot {
            nk_color normal;
            nk_color hover;
            nk_color active;
        };

        struct TextInputTextColorSnapshot {
            nk_color cursorNormal;
            nk_color cursorHover;
            nk_color cursorTextNormal;
            nk_color cursorTextHover;
            nk_color textNormal;
            nk_color textHover;
            nk_color textActive;
            nk_color selectedTextNormal;
            nk_color selectedTextHover;
        };

        nk_context* m_ctx = nullptr;
        VkExtent2D m_framebufferExtent{};
        const char* m_activePanelTitle = nullptr;
        std::vector<nk_color> m_textColorStack;
        std::vector<PanelBodyStyleSnapshot> m_panelBodyColorStack;
        std::vector<StyleItemStateSnapshot> m_panelTitleBarColorStack;
        std::vector<nk_color> m_panelBorderColorStack;
        std::vector<ColorStateSnapshot> m_panelTitleTextColorStack;
        std::vector<ColorStateSnapshot> m_radioButtonTextColorStack;
        std::vector<StyleItemStateSnapshot> m_buttonBackgroundColorStack;
        std::vector<nk_style_item> m_buttonHoverBackgroundColorStack;
        std::vector<nk_style_item> m_buttonClickedBackgroundColorStack;
        std::vector<float> m_buttonHeightStack;
        std::vector<float> m_buttonWidthStack;
        std::vector<float> m_buttonCornerRadiusStack;
        std::vector<float> m_buttonBorderThicknessStack;
        std::vector<struct nk_vec2> m_buttonPaddingStack;
        std::vector<Spherical::ButtonStyle> m_buttonStyleStack;
        std::vector<nk_color> m_buttonHighlightColorStack;
        std::vector<nk_color> m_buttonShadowColorStack;
        std::vector<nk_color> m_buttonBorderColorStack;
        std::vector<ColorStateSnapshot> m_buttonTextColorStack;
        std::vector<StyleItemStateSnapshot> m_textInputBackgroundColorStack;
        std::vector<TextInputTextColorSnapshot> m_textInputTextColorStack;

        float current_font_height() const {
            if (m_ctx != nullptr && m_ctx->style.font != nullptr) {
                return m_ctx->style.font->height;
            }

            if (nk_user_font* defaultFont = Spherical::FontRenderer::GetFontHandle(Spherical::FontStyle::Regular)) {
                return defaultFont->height;
            }

            return 12.0f;
        }

        float label_row_height() const {
            return std::ceil(current_font_height() * 1.8f);
        }

        float control_row_height() const {
            return std::ceil(current_font_height() * 2.0f);
        }

        float button_row_height() const {
            const struct nk_vec2 padding = current_button_padding();
            const float border = current_button_border_thickness();
            const float requestedHeight = m_buttonHeightStack.empty()
                ? std::ceil(current_font_height() * 2.25f)
                : m_buttonHeightStack.back();
            const float minHeight = std::ceil(current_font_height() + 2.0f * padding.y + 2.0f * border + 2.0f);
            return std::max(requestedHeight, minHeight);
        }

        float spacing_row_height() const {
            return std::max(4.0f, std::ceil(current_font_height() * 0.8f));
        }
        
        //scrollbar drag helpers
        void release_scrollbar_drag_if_needed(const char* title) {
            if (title == nullptr) {
                return;
            }

            if (g_scrollbarDrag.active && g_scrollbarDrag.windowTitle == title) {
                g_scrollbarDrag = {};
            }
        }

        void apply_active_vertical_scrollbar_drag(const char* title) {
            if (m_ctx == nullptr || title == nullptr) {
                return;
            }

            if (!g_scrollbarDrag.active || g_scrollbarDrag.windowTitle != title) {
                return;
            }

            if (!IsLeftMouseDown(m_ctx)) {
                release_scrollbar_drag_if_needed(title);
                return;
            }

            if (g_scrollbarDrag.trackH <= 0.0f || g_scrollbarDrag.thumbH <= 0.0f || g_scrollbarDrag.maxScrollY <= 0.0f) {
                return;
            }

            nk_uint scrollX = 0;
            nk_uint scrollY = 0;
            nk_window_get_scroll(m_ctx, &scrollX, &scrollY);

            const float dragTravel = std::max(1.0f, g_scrollbarDrag.trackH - g_scrollbarDrag.thumbH);
            float desiredThumbY = m_ctx->input.mouse.pos.y - g_scrollbarDrag.grabOffsetY;
            desiredThumbY = std::clamp(desiredThumbY, g_scrollbarDrag.trackY, g_scrollbarDrag.trackY + dragTravel);

            const float normalizedThumb = (desiredThumbY - g_scrollbarDrag.trackY) / dragTravel;
            const float desiredScrollY = std::clamp(normalizedThumb * g_scrollbarDrag.maxScrollY, 0.0f, g_scrollbarDrag.maxScrollY);

            nk_window_set_scroll(m_ctx, scrollX, static_cast<nk_uint>(desiredScrollY + 0.5f));
            m_ctx->input.mouse.scroll_delta.y = 0.0f;
        }

        void refresh_vertical_scrollbar_drag_state(const char* title) {
            if (m_ctx == nullptr || title == nullptr) {
                return;
            }

            nk_panel* panel = nk_window_get_panel(m_ctx);
            if (panel == nullptr) {
                return;
            }

            nk_uint scrollX = 0;
            nk_uint scrollY = 0;
            nk_window_get_scroll(m_ctx, &scrollX, &scrollY);

            const struct nk_rect windowBounds = nk_window_get_bounds(m_ctx);
            const struct nk_rect contentRegion = nk_window_get_content_region(m_ctx);

            const float visibleContentHeight = contentRegion.h;
            // nk_end() advances panel->at_y by panel->row.height; include it here since we run before nk_end().
            const float estimatedContentBottomY = panel->at_y + panel->row.height;
            const float totalContentHeight = std::max(estimatedContentBottomY - contentRegion.y, visibleContentHeight);
            const float maxScrollY = std::max(0.0f, totalContentHeight - visibleContentHeight);

            if (maxScrollY <= 0.0f) {
                release_scrollbar_drag_if_needed(title);
                return;
            }

            const float scrollbarWidth = m_ctx->style.window.scrollbar_size.x;
            if (scrollbarWidth <= 0.0f) {
                release_scrollbar_drag_if_needed(title);
                return;
            }

            struct nk_rect track{};
            track.x = windowBounds.x + windowBounds.w - scrollbarWidth;
            track.y = contentRegion.y;
            track.w = scrollbarWidth;
            track.h = visibleContentHeight;

            // Expand hit area to include the left-edge border/padding around the visual scrollbar.
            const float edgeExpand = std::max(
                2.0f,
                m_ctx->style.window.border + m_ctx->style.scrollv.border + m_ctx->style.scrollv.padding.x
            );

            struct nk_rect trackHit = track;
            trackHit.x -= edgeExpand;
            trackHit.w += edgeExpand;

            if (track.h <= 0.0f) {
                release_scrollbar_drag_if_needed(title);
                return;
            }

            const float thumbRatio = visibleContentHeight / totalContentHeight;
            const float thumbH = std::max(16.0f, track.h * thumbRatio);
            const float thumbTravel = std::max(1.0f, track.h - thumbH);
            const float normalizedScroll = (maxScrollY > 0.0f) ? (static_cast<float>(scrollY) / maxScrollY) : 0.0f;

            struct nk_rect thumb{};
            thumb.x = track.x;
            thumb.y = track.y + normalizedScroll * thumbTravel;
            thumb.w = track.w;
            thumb.h = thumbH;

            const bool leftPressed = WasLeftMousePressed(m_ctx);
            const bool leftDown = IsLeftMouseDown(m_ctx);

            if (!leftDown) {
                release_scrollbar_drag_if_needed(title);
                return;
            }

            if (!g_scrollbarDrag.active && leftPressed) {
                const bool clickedThumb = IsMouseInsideRect(m_ctx, thumb);
                const bool clickedTrack = IsMouseInsideRect(m_ctx, trackHit);

                if (clickedThumb || clickedTrack) {
                    g_scrollbarDrag.active = true;
                    g_scrollbarDrag.windowTitle = title;
                    g_scrollbarDrag.dragStartMouseY = m_ctx->input.mouse.pos.y;
                    g_scrollbarDrag.dragStartScrollY = static_cast<float>(scrollY);
                    g_scrollbarDrag.trackY = track.y;
                    g_scrollbarDrag.trackH = track.h;
                    g_scrollbarDrag.thumbH = thumb.h;
                    g_scrollbarDrag.maxScrollY = maxScrollY;

                    if (clickedThumb) {
                        // Preserve exact grab position inside thumb for 1:1 dragging.
                        g_scrollbarDrag.grabOffsetY = m_ctx->input.mouse.pos.y - thumb.y;
                    } else {
                        // Track click: snap thumb center to cursor, then continue drag with same math.
                        g_scrollbarDrag.grabOffsetY = g_scrollbarDrag.thumbH * 0.5f;
                    }

                    // Prevent Nuklear's built-in track-click paging from overriding custom behavior.
                    m_ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
                }
            }

            if (g_scrollbarDrag.active && g_scrollbarDrag.windowTitle == title) {
                g_scrollbarDrag.trackY = track.y;
                g_scrollbarDrag.trackH = track.h;
                g_scrollbarDrag.thumbH = thumb.h;
                g_scrollbarDrag.maxScrollY = maxScrollY;
                m_ctx->input.mouse.scroll_delta.y = 0.0f;
            }
        }
        
        static nk_color ToNkColor(const Spherical::UIColor& c) {
            auto toByte = [](float v) -> nk_byte {
                return static_cast<nk_byte>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            return nk_rgba(toByte(c.r), toByte(c.g), toByte(c.b), toByte(c.a));
        }

        static nk_style_item ToNkStyleItem(const Spherical::UIColor& c) {
            return nk_style_item_color(ToNkColor(c));
        }

        static struct nk_vec2 ToNkVec2(const Spherical::UIVec2& v) {
            return nk_vec2(v.x, v.y);
        }

        static nk_color BlendColor(const nk_color& from, const nk_color& to, float t) {
            const float clampedT = std::clamp(t, 0.0f, 1.0f);
            const auto blendChannel = [clampedT](nk_byte a, nk_byte b) -> nk_byte {
                const float blended = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * clampedT;
                return static_cast<nk_byte>(std::clamp(blended, 0.0f, 255.0f) + 0.5f);
            };
            return nk_rgba(
                blendChannel(from.r, to.r),
                blendChannel(from.g, to.g),
                blendChannel(from.b, to.b),
                blendChannel(from.a, to.a));
        }

        static nk_color LightenColor(const nk_color& color, float amount) {
            return BlendColor(color, nk_rgb(255, 255, 255), amount);
        }

        static nk_color DarkenColor(const nk_color& color, float amount) {
            return BlendColor(color, nk_rgb(0, 0, 0), amount);
        }

        static nk_color GetStyleItemColor(const nk_style_item& item, const nk_color& fallback) {
            if (item.type == NK_STYLE_ITEM_COLOR) {
                return item.data.color;
            }
            return fallback;
        }

        float current_button_corner_radius() const {
            if (!m_buttonCornerRadiusStack.empty()) {
                return m_buttonCornerRadiusStack.back();
            }
            return (m_ctx != nullptr) ? m_ctx->style.button.rounding : 0.0f;
        }

        float current_button_width(float availableWidth) const {
            if (m_buttonWidthStack.empty()) {
                return availableWidth;
            }

            const float requestedWidth = m_buttonWidthStack.back();
            if (requestedWidth <= 0.0f) {
                return availableWidth;
            }

            return std::min(requestedWidth, availableWidth);
        }

        float current_button_border_thickness() const {
            if (!m_buttonBorderThicknessStack.empty()) {
                return m_buttonBorderThicknessStack.back();
            }
            return (m_ctx != nullptr) ? m_ctx->style.button.border : 0.0f;
        }

        struct nk_vec2 current_button_padding() const {
            if (!m_buttonPaddingStack.empty()) {
                return m_buttonPaddingStack.back();
            }
            return (m_ctx != nullptr) ? m_ctx->style.button.padding : nk_vec2(10.0f, 6.0f);
        }

        Spherical::ButtonStyle current_button_style() const {
            if (!m_buttonStyleStack.empty()) {
                return m_buttonStyleStack.back();
            }
            return Spherical::ButtonStyle::Flat;
        }

        nk_color current_button_highlight_color(const nk_color& baseColor) const {
            if (!m_buttonHighlightColorStack.empty()) {
                return m_buttonHighlightColorStack.back();
            }
            return LightenColor(baseColor, 0.55f);
        }

        nk_color current_button_shadow_color(const nk_color& baseColor) const {
            if (!m_buttonShadowColorStack.empty()) {
                return m_buttonShadowColorStack.back();
            }
            return DarkenColor(baseColor, 0.45f);
        }

        void draw_button_bevel(nk_command_buffer* buffer, const struct nk_rect& bounds, float borderThickness,
                               const nk_color& baseColor, bool pressed) const {
            if (buffer == nullptr || bounds.w <= 2.0f || bounds.h <= 2.0f) {
                return;
            }

            const float inset = std::max(1.0f, borderThickness);
            const float left = bounds.x + inset;
            const float top = bounds.y + inset;
            const float right = bounds.x + bounds.w - inset - 1.0f;
            const float bottom = bounds.y + bounds.h - inset - 1.0f;
            if (right <= left || bottom <= top) {
                return;
            }

            nk_color topLeft = current_button_highlight_color(baseColor);
            nk_color bottomRight = current_button_shadow_color(baseColor);
            if (pressed) {
                std::swap(topLeft, bottomRight);
            }

            nk_stroke_line(buffer, left, top, right, top, 1.0f, topLeft);
            nk_stroke_line(buffer, left, top, left, bottom, 1.0f, topLeft);
            nk_stroke_line(buffer, left, bottom, right, bottom, 1.0f, bottomRight);
            nk_stroke_line(buffer, right, top, right, bottom, 1.0f, bottomRight);

            if ((right - left) >= 4.0f && (bottom - top) >= 4.0f) {
                const float innerLeft = left + 1.0f;
                const float innerTop = top + 1.0f;
                const float innerRight = right - 1.0f;
                const float innerBottom = bottom - 1.0f;
                const nk_color softTopLeft = BlendColor(topLeft, baseColor, 0.45f);
                const nk_color softBottomRight = BlendColor(bottomRight, baseColor, 0.45f);
                nk_stroke_line(buffer, innerLeft, innerTop, innerRight, innerTop, 1.0f, softTopLeft);
                nk_stroke_line(buffer, innerLeft, innerTop, innerLeft, innerBottom, 1.0f, softTopLeft);
                nk_stroke_line(buffer, innerLeft, innerBottom, innerRight, innerBottom, 1.0f, softBottomRight);
                nk_stroke_line(buffer, innerRight, innerTop, innerRight, innerBottom, 1.0f, softBottomRight);
            }
        }

        bool draw_custom_button(const char* label) {
            if (m_ctx == nullptr || m_ctx->current == nullptr || m_ctx->current->layout == nullptr) {
                return false;
            }

            nk_window* win = m_ctx->current;
            nk_panel* layout = win->layout;
            const nk_style_button* style = &m_ctx->style.button;

            struct nk_rect bounds{};
            const nk_widget_layout_states widgetState = nk_widget(&bounds, m_ctx);
            if (!widgetState) {
                return false;
            }

            const float centeredWidth = current_button_width(bounds.w);
            if (centeredWidth < bounds.w) {
                bounds.x += std::max(0.0f, (bounds.w - centeredWidth) * 0.5f);
                bounds.w = centeredWidth;
            }

            const bool isReadOnly = (widgetState == NK_WIDGET_DISABLED) || ((layout->flags & NK_WINDOW_ROM) != 0);
            const bool hovered = !isReadOnly && IsMouseInsideRect(m_ctx, bounds);
            const bool pressed = hovered && IsLeftMouseDown(m_ctx);
            const bool activated = hovered && WasLeftMousePressed(m_ctx);

            const nk_style_item& backgroundItem = pressed ? style->active : (hovered ? style->hover : style->normal);
            const nk_color fallbackBackground = pressed ? m_ctx->style.button.text_background : m_ctx->style.window.background;
            const nk_color backgroundColor = GetStyleItemColor(backgroundItem, fallbackBackground);
            const float borderThickness = std::max(0.0f, current_button_border_thickness());
            const float cornerRadius = std::max(0.0f, current_button_corner_radius());
            const struct nk_vec2 padding = current_button_padding();

            nk_fill_rect(&win->buffer, bounds, cornerRadius, backgroundColor);

            if (current_button_style() == Spherical::ButtonStyle::Embossed) {
                draw_button_bevel(&win->buffer, bounds, borderThickness, backgroundColor, pressed);
            }

            if (borderThickness > 0.0f) {
                nk_stroke_rect(&win->buffer, bounds, cornerRadius, borderThickness, style->border_color);
            }

            const nk_color labelColor = pressed ? style->text_active : (hovered ? style->text_hover : style->text_normal);
            const nk_user_font* buttonFont = m_ctx->style.font;
            if (buttonFont == nullptr) {
                buttonFont = Spherical::FontRenderer::GetFontHandle(Spherical::FontStyle::Regular);
            }
            if (buttonFont != nullptr && label != nullptr) {
                const int labelLength = static_cast<int>(std::strlen(label));
                const float contentX = bounds.x + borderThickness + padding.x;
                const float contentY = bounds.y + borderThickness + padding.y;
                const float contentW = std::max(1.0f, bounds.w - 2.0f * (borderThickness + padding.x));
                const float contentH = std::max(1.0f, bounds.h - 2.0f * (borderThickness + padding.y));
                float textX = contentX;
                const float textWidth = buttonFont->width(buttonFont->userdata, buttonFont->height, label, labelLength);
                if (style->text_alignment & NK_TEXT_ALIGN_CENTERED) {
                    textX = contentX + std::max(0.0f, (contentW - textWidth) * 0.5f);
                } else if (style->text_alignment & NK_TEXT_ALIGN_RIGHT) {
                    textX = std::max(contentX, contentX + contentW - textWidth);
                }

                float textY = contentY;
                if (style->text_alignment & NK_TEXT_ALIGN_BOTTOM) {
                    textY = contentY + std::max(0.0f, contentH - buttonFont->height);
                } else {
                    textY = contentY + std::max(0.0f, (contentH - buttonFont->height) * 0.5f);
                }

                struct nk_rect labelBounds = nk_rect(textX, textY, std::max(1.0f, contentW), buttonFont->height);
                if (pressed) {
                    labelBounds.x += 1.0f;
                    labelBounds.y += 1.0f;
                }
                nk_draw_text(&win->buffer, labelBounds, label, labelLength, buttonFont, nk_rgba(0, 0, 0, 0), labelColor);
            }

            return activated != 0;
        }

        void restore_last_text_color() {
            if (m_ctx == nullptr || m_textColorStack.empty()) {
                return;
            }
            m_ctx->style.text.color = m_textColorStack.back();
            m_textColorStack.pop_back();
        }

        void restore_last_panel_body_color() {
            if (m_ctx == nullptr || m_panelBodyColorStack.empty()) {
                return;
            }
            const PanelBodyStyleSnapshot snapshot = m_panelBodyColorStack.back();
            m_panelBodyColorStack.pop_back();
            m_ctx->style.window.background = snapshot.background;
            m_ctx->style.window.fixed_background = snapshot.fixedBackground;
        }

        void restore_last_panel_title_bar_color() {
            if (m_ctx == nullptr || m_panelTitleBarColorStack.empty()) {
                return;
            }
            const StyleItemStateSnapshot snapshot = m_panelTitleBarColorStack.back();
            m_panelTitleBarColorStack.pop_back();
            m_ctx->style.window.header.normal = snapshot.normal;
            m_ctx->style.window.header.hover = snapshot.hover;
            m_ctx->style.window.header.active = snapshot.active;
        }

        void restore_last_panel_border_color() {
            if (m_ctx == nullptr || m_panelBorderColorStack.empty()) {
                return;
            }
            m_ctx->style.window.border_color = m_panelBorderColorStack.back();
            m_panelBorderColorStack.pop_back();
        }

        void restore_last_panel_title_text_color() {
            if (m_ctx == nullptr || m_panelTitleTextColorStack.empty()) {
                return;
            }
            const ColorStateSnapshot snapshot = m_panelTitleTextColorStack.back();
            m_panelTitleTextColorStack.pop_back();
            m_ctx->style.window.header.label_normal = snapshot.normal;
            m_ctx->style.window.header.label_hover = snapshot.hover;
            m_ctx->style.window.header.label_active = snapshot.active;
        }

        void restore_last_radio_button_text_color() {
            if (m_ctx == nullptr || m_radioButtonTextColorStack.empty()) {
                return;
            }
            const ColorStateSnapshot snapshot = m_radioButtonTextColorStack.back();
            m_radioButtonTextColorStack.pop_back();
            m_ctx->style.option.text_normal = snapshot.normal;
            m_ctx->style.option.text_hover = snapshot.hover;
            m_ctx->style.option.text_active = snapshot.active;
        }

        void restore_last_button_background_color() {
            if (m_ctx == nullptr || m_buttonBackgroundColorStack.empty()) {
                return;
            }
            const StyleItemStateSnapshot snapshot = m_buttonBackgroundColorStack.back();
            m_buttonBackgroundColorStack.pop_back();
            m_ctx->style.button.normal = snapshot.normal;
            m_ctx->style.button.hover = snapshot.hover;
            m_ctx->style.button.active = snapshot.active;
        }

        void restore_last_button_hover_background_color() {
            if (m_ctx == nullptr || m_buttonHoverBackgroundColorStack.empty()) {
                return;
            }
            m_ctx->style.button.hover = m_buttonHoverBackgroundColorStack.back();
            m_buttonHoverBackgroundColorStack.pop_back();
        }

        void restore_last_button_clicked_background_color() {
            if (m_ctx == nullptr || m_buttonClickedBackgroundColorStack.empty()) {
                return;
            }
            m_ctx->style.button.active = m_buttonClickedBackgroundColorStack.back();
            m_buttonClickedBackgroundColorStack.pop_back();
        }

        void restore_last_button_height() {
            if (!m_buttonHeightStack.empty()) {
                m_buttonHeightStack.pop_back();
            }
        }

        void restore_last_button_width() {
            if (!m_buttonWidthStack.empty()) {
                m_buttonWidthStack.pop_back();
            }
        }

        void restore_last_button_corner_radius() {
            if (!m_buttonCornerRadiusStack.empty()) {
                m_buttonCornerRadiusStack.pop_back();
            }
        }

        void restore_last_button_border_thickness() {
            if (!m_buttonBorderThicknessStack.empty()) {
                m_buttonBorderThicknessStack.pop_back();
            }
        }

        void restore_last_button_padding() {
            if (!m_buttonPaddingStack.empty()) {
                m_buttonPaddingStack.pop_back();
            }
        }

        void restore_last_button_style() {
            if (!m_buttonStyleStack.empty()) {
                m_buttonStyleStack.pop_back();
            }
        }

        void restore_last_button_highlight_color() {
            if (!m_buttonHighlightColorStack.empty()) {
                m_buttonHighlightColorStack.pop_back();
            }
        }

        void restore_last_button_shadow_color() {
            if (!m_buttonShadowColorStack.empty()) {
                m_buttonShadowColorStack.pop_back();
            }
        }

        void restore_last_button_border_color() {
            if (m_ctx == nullptr || m_buttonBorderColorStack.empty()) {
                return;
            }
            m_ctx->style.button.border_color = m_buttonBorderColorStack.back();
            m_buttonBorderColorStack.pop_back();
        }

        void restore_last_button_text_color() {
            if (m_ctx == nullptr || m_buttonTextColorStack.empty()) {
                return;
            }
            const ColorStateSnapshot snapshot = m_buttonTextColorStack.back();
            m_buttonTextColorStack.pop_back();
            m_ctx->style.button.text_normal = snapshot.normal;
            m_ctx->style.button.text_hover = snapshot.hover;
            m_ctx->style.button.text_active = snapshot.active;
        }

        void restore_last_text_input_background_color() {
            if (m_ctx == nullptr || m_textInputBackgroundColorStack.empty()) {
                return;
            }
            const StyleItemStateSnapshot snapshot = m_textInputBackgroundColorStack.back();
            m_textInputBackgroundColorStack.pop_back();
            m_ctx->style.edit.normal = snapshot.normal;
            m_ctx->style.edit.hover = snapshot.hover;
            m_ctx->style.edit.active = snapshot.active;
        }

        void restore_last_text_input_text_color() {
            if (m_ctx == nullptr || m_textInputTextColorStack.empty()) {
                return;
            }
            const TextInputTextColorSnapshot snapshot = m_textInputTextColorStack.back();
            m_textInputTextColorStack.pop_back();
            m_ctx->style.edit.cursor_text_normal = snapshot.cursorTextNormal;
            m_ctx->style.edit.cursor_text_hover = snapshot.cursorTextHover;
            m_ctx->style.edit.text_normal = snapshot.textNormal;
            m_ctx->style.edit.text_hover = snapshot.textHover;
            m_ctx->style.edit.text_active = snapshot.textActive;
            m_ctx->style.edit.selected_text_normal = snapshot.selectedTextNormal;
            m_ctx->style.edit.selected_text_hover = snapshot.selectedTextHover;
            m_ctx->style.edit.cursor_normal = snapshot.cursorNormal;
            m_ctx->style.edit.cursor_hover = snapshot.cursorHover;
        }
        

    public:
        UIPainterImpl(nk_context* ctx, VkExtent2D extent) : m_ctx(ctx), m_framebufferExtent(extent) {}

        ~UIPainterImpl() override {
            while (!m_textInputTextColorStack.empty()) {
                restore_last_text_input_text_color();
            }
            while (!m_textInputBackgroundColorStack.empty()) {
                restore_last_text_input_background_color();
            }
            while (!m_buttonShadowColorStack.empty()) {
                restore_last_button_shadow_color();
            }
            while (!m_buttonHighlightColorStack.empty()) {
                restore_last_button_highlight_color();
            }
            while (!m_buttonStyleStack.empty()) {
                restore_last_button_style();
            }
            while (!m_buttonPaddingStack.empty()) {
                restore_last_button_padding();
            }
            while (!m_buttonBorderThicknessStack.empty()) {
                restore_last_button_border_thickness();
            }
            while (!m_buttonCornerRadiusStack.empty()) {
                restore_last_button_corner_radius();
            }
            while (!m_buttonWidthStack.empty()) {
                restore_last_button_width();
            }
            while (!m_buttonHeightStack.empty()) {
                restore_last_button_height();
            }
            while (!m_buttonTextColorStack.empty()) {
                restore_last_button_text_color();
            }
            while (!m_buttonBorderColorStack.empty()) {
                restore_last_button_border_color();
            }
            while (!m_buttonClickedBackgroundColorStack.empty()) {
                restore_last_button_clicked_background_color();
            }
            while (!m_buttonHoverBackgroundColorStack.empty()) {
                restore_last_button_hover_background_color();
            }
            while (!m_buttonBackgroundColorStack.empty()) {
                restore_last_button_background_color();
            }
            while (!m_radioButtonTextColorStack.empty()) {
                restore_last_radio_button_text_color();
            }
            while (!m_panelTitleTextColorStack.empty()) {
                restore_last_panel_title_text_color();
            }
            while (!m_panelBorderColorStack.empty()) {
                restore_last_panel_border_color();
            }
            while (!m_panelTitleBarColorStack.empty()) {
                restore_last_panel_title_bar_color();
            }
            while (!m_panelBodyColorStack.empty()) {
                restore_last_panel_body_color();
            }
            while (!m_textColorStack.empty()) {
                restore_last_text_color();
            }
        }

        bool begin_panel(const char* title, int x, int y, int width, int height) override {
            // Use the title font for the panel header
            push_font(Spherical::FontStyle::Title);
            const bool result = nk_begin(m_ctx, title, nk_rect(x, y, width, height), NK_WINDOW_BORDER | NK_WINDOW_TITLE) != 0;
            // Pop back to the regular font for the panel content
            pop_font();

            if (result) {
                apply_active_vertical_scrollbar_drag(title);
            }

            // Keep the active panel title so end_panel can refresh scrollbar metrics after content layout.
            m_activePanelTitle = result ? title : nullptr;
            if (!result) {
                release_scrollbar_drag_if_needed(title);
            }

            return result;
        }

        void end_panel() override {
            if (m_activePanelTitle != nullptr) {
                // Refresh drag geometry and detect drag-start after content layout is known for this frame.
                refresh_vertical_scrollbar_drag_state(m_activePanelTitle);
            }
            nk_end(m_ctx);
            m_activePanelTitle = nullptr;
        }

        void label(const char* text) override {
            nk_layout_row_dynamic(m_ctx, label_row_height(), 1);
            nk_label(m_ctx, text, NK_TEXT_LEFT);
        }

        void push_font(Spherical::FontStyle style) override {
            const nk_user_font* font = Spherical::FontRenderer::GetFontHandle(style);
            if (font == nullptr && m_ctx != nullptr) {
                font = m_ctx->style.font;
            }
            if (font != nullptr) {
                nk_style_push_font(m_ctx, font);
            }
        }

        void pop_font() override {
            nk_style_pop_font(m_ctx);
        }

        void spacing() override {
            nk_layout_row_dynamic(m_ctx, spacing_row_height(), 1);
            nk_spacing(m_ctx, 1);
        }
        
        void slider_float(const char* label, float* value, float min, float max, float step) override {
            nk_layout_row_dynamic(m_ctx, control_row_height(), 2);
            nk_label(m_ctx, label, NK_TEXT_LEFT);

            if (m_ctx == nullptr || value == nullptr || m_ctx->current == nullptr || m_ctx->current->layout == nullptr) {
                return;
            }

            nk_window* win = m_ctx->current;
            nk_panel* layout = win->layout;
            const nk_style* style = &m_ctx->style;
            const nk_style_slider* sliderStyle = &style->slider;

            struct nk_rect bounds{};
            const nk_widget_layout_states widgetState = nk_widget(&bounds, m_ctx);
            if (!widgetState) {
                return;
            }

            const bool isReadOnly = (widgetState == NK_WIDGET_DISABLED) || ((layout->flags & NK_WINDOW_ROM) != 0);
            nk_input* in = isReadOnly ? nullptr : &m_ctx->input;

            // Match Nuklear slider geometry (padding + optional inc/dec buttons).
            struct nk_rect track = bounds;
            track.x += sliderStyle->padding.x;
            track.y += sliderStyle->padding.y;
            track.h = NK_MAX(track.h, 2.0f * sliderStyle->padding.y) - 2.0f * sliderStyle->padding.y;
            track.w = NK_MAX(track.w, 2.0f * sliderStyle->padding.x + sliderStyle->cursor_size.x) - 2.0f * sliderStyle->padding.x;

            if (sliderStyle->show_buttons) {
                const float buttonW = track.h;
                track.x += buttonW + sliderStyle->spacing.x;
                track.w -= (2.0f * buttonW + 2.0f * sliderStyle->spacing.x);
            }

            const bool leftDown = IsLeftMouseDown(m_ctx);
            const bool leftPressed = WasLeftMousePressed(m_ctx);

            if (!leftDown && g_sliderTrackDrag.active && g_sliderTrackDrag.valueRef == value) {
                g_sliderTrackDrag = {};
            }

            if (in != nullptr && leftPressed && IsMouseInsideRect(m_ctx, track)) {
                g_sliderTrackDrag.active = true;
                g_sliderTrackDrag.valueRef = value;
            }

            const float sliderMin = std::min(min, max);
            const float sliderMax = std::max(min, max);
            const float safeStep = (step > 0.0f) ? step : 1.0f;

            const bool draggingThisSlider =
                (in != nullptr) && g_sliderTrackDrag.active && (g_sliderTrackDrag.valueRef == value) && leftDown;

            if (draggingThisSlider) {
                const float t = (track.w > 0.0f) ? ((m_ctx->input.mouse.pos.x - track.x) / track.w) : 0.0f;
                float newValue = sliderMin + std::clamp(t, 0.0f, 1.0f) * (sliderMax - sliderMin);
                newValue = sliderMin + std::round((newValue - sliderMin) / safeStep) * safeStep;
                *value = std::clamp(newValue, sliderMin, sliderMax);
            }

            *value = std::clamp(*value, sliderMin, sliderMax);

            const bool hovered = (in != nullptr) && IsMouseInsideRect(m_ctx, track);
            const bool active = draggingThisSlider;
            const nk_color barColor = active ? sliderStyle->bar_active : (hovered ? sliderStyle->bar_hover : sliderStyle->bar_normal);
            const nk_style_item* cursorItem = active
                ? &sliderStyle->cursor_active
                : (hovered ? &sliderStyle->cursor_hover : &sliderStyle->cursor_normal);

            struct nk_rect bar{};
            bar.x = track.x;
            bar.y = (track.y + track.h * 0.5f) - (sliderStyle->bar_height * 0.5f);
            bar.w = track.w;
            bar.h = sliderStyle->bar_height;

            const float ratio = (sliderMax > sliderMin) ? ((*value - sliderMin) / (sliderMax - sliderMin)) : 0.0f;
            const float clampedRatio = std::clamp(ratio, 0.0f, 1.0f);

            struct nk_rect fill = bar;
            fill.w *= clampedRatio;

            struct nk_rect cursor{};
            cursor.w = sliderStyle->cursor_size.x;
            cursor.h = sliderStyle->cursor_size.y;
            cursor.x = track.x + track.w * clampedRatio - cursor.w * 0.5f;
            cursor.y = (track.y + track.h * 0.5f) - cursor.h * 0.5f;

            nk_fill_rect(&win->buffer, bounds, sliderStyle->rounding, style->window.background);
            nk_stroke_rect(&win->buffer, bounds, sliderStyle->rounding, sliderStyle->border, sliderStyle->border_color);
            nk_fill_rect(&win->buffer, bar, sliderStyle->rounding, barColor);
            nk_fill_rect(&win->buffer, fill, sliderStyle->rounding, sliderStyle->bar_filled);

            if (cursorItem->type == NK_STYLE_ITEM_IMAGE) {
                nk_draw_image(&win->buffer, cursor, &cursorItem->data.image, nk_rgb(255, 255, 255));
            } else {
                nk_fill_circle(&win->buffer, cursor, cursorItem->data.color);
            }
        }

        bool button(const char* label) override {
            nk_layout_row_dynamic(m_ctx, button_row_height(), 1);
            return draw_custom_button(label != nullptr ? label : "");
        }

        void text_input(const char* label, char* buffer, size_t bufferSize) override {
            nk_layout_row_dynamic(m_ctx, control_row_height(), 1);
            nk_label(m_ctx, label, NK_TEXT_LEFT);
            nk_layout_row_dynamic(m_ctx, control_row_height(), 1);
            nk_edit_string_zero_terminated(m_ctx, NK_EDIT_FIELD, buffer,
                                           static_cast<int>(bufferSize), nk_filter_default);
        }

        bool radio_button(const char* label, int* activeIndex, int value) override {
            if (activeIndex == nullptr) {
                // nothing to modify
                return false;
            }

            // Use same row height as other controls
            nk_layout_row_dynamic(m_ctx, control_row_height(), 1);

            // Remember previous selection so we can detect a change
            const int prev = *activeIndex;

            // Nuklear draws the radio and its label together
            // We call nk_radio_label which will set *activeIndex to 'value' when user selects it.
            if (nk_option_label(m_ctx, label, static_cast<nk_bool>(*activeIndex == value))) {
                *activeIndex = value;
            }

            return (*activeIndex != prev);
        }
        
        uint32_t get_framebuffer_width() const override {
            return m_framebufferExtent.width;
        }

        uint32_t get_framebuffer_height() const override {
            return m_framebufferExtent.height;
        }
        
        void push_text_color(const Spherical::UIColor& color) override
        {
            if (m_ctx == nullptr) {
                return;
            }
            m_textColorStack.push_back(m_ctx->style.text.color);
            m_ctx->style.text.color = ToNkColor(color);
        };
        
        void pop_text_color() override {
            restore_last_text_color();
        };

        void push_panel_body_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_panelBodyColorStack.push_back({
                m_ctx->style.window.background,
                m_ctx->style.window.fixed_background
            });
            m_ctx->style.window.background = ToNkColor(color);
            m_ctx->style.window.fixed_background = ToNkStyleItem(color);
        };
        
        void pop_panel_body_color() override {
            restore_last_panel_body_color();
        };

        void push_panel_title_bar_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_panelTitleBarColorStack.push_back({
                m_ctx->style.window.header.normal,
                m_ctx->style.window.header.hover,
                m_ctx->style.window.header.active
            });

            const nk_style_item styleItem = ToNkStyleItem(color);
            m_ctx->style.window.header.normal = styleItem;
            m_ctx->style.window.header.hover = styleItem;
            m_ctx->style.window.header.active = styleItem;
        };

        void pop_panel_title_bar_color() override {
            restore_last_panel_title_bar_color();
        };

        void push_panel_border_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_panelBorderColorStack.push_back(m_ctx->style.window.border_color);
            m_ctx->style.window.border_color = ToNkColor(color);
        };

        void pop_panel_border_color() override {
            restore_last_panel_border_color();
        };

        void push_panel_title_text_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_panelTitleTextColorStack.push_back({
                m_ctx->style.window.header.label_normal,
                m_ctx->style.window.header.label_hover,
                m_ctx->style.window.header.label_active
            });

            const nk_color nkColor = ToNkColor(color);
            m_ctx->style.window.header.label_normal = nkColor;
            m_ctx->style.window.header.label_hover = nkColor;
            m_ctx->style.window.header.label_active = nkColor;
        };

        void pop_panel_title_text_color() override {
            restore_last_panel_title_text_color();
        };

        void push_radio_button_text_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_radioButtonTextColorStack.push_back({
                m_ctx->style.option.text_normal,
                m_ctx->style.option.text_hover,
                m_ctx->style.option.text_active
            });

            const nk_color nkColor = ToNkColor(color);
            m_ctx->style.option.text_normal = nkColor;
            m_ctx->style.option.text_hover = nkColor;
            m_ctx->style.option.text_active = nkColor;
        };

        void pop_radio_button_text_color() override {
            restore_last_radio_button_text_color();
        };

        void push_button_background_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_buttonBackgroundColorStack.push_back({
                m_ctx->style.button.normal,
                m_ctx->style.button.hover,
                m_ctx->style.button.active
            });

            const nk_style_item styleItem = ToNkStyleItem(color);
            m_ctx->style.button.normal = styleItem;
            m_ctx->style.button.hover = styleItem;
            m_ctx->style.button.active = styleItem;
        };

        void pop_button_background_color() override {
            restore_last_button_background_color();
        };

        void push_button_hover_background_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_buttonHoverBackgroundColorStack.push_back(m_ctx->style.button.hover);
            m_ctx->style.button.hover = ToNkStyleItem(color);
        };

        void pop_button_hover_background_color() override {
            restore_last_button_hover_background_color();
        };

        void push_button_clicked_background_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_buttonClickedBackgroundColorStack.push_back(m_ctx->style.button.active);
            m_ctx->style.button.active = ToNkStyleItem(color);
        };

        void pop_button_clicked_background_color() override {
            restore_last_button_clicked_background_color();
        };

        void push_button_height(float height) override {
            m_buttonHeightStack.push_back(std::max(1.0f, height));
        };

        void pop_button_height() override {
            restore_last_button_height();
        };

        void push_button_width(float width) override {
            m_buttonWidthStack.push_back(width);
        };

        void pop_button_width() override {
            restore_last_button_width();
        };

        void push_button_corner_radius(float radius) override {
            m_buttonCornerRadiusStack.push_back(std::max(0.0f, radius));
        };

        void pop_button_corner_radius() override {
            restore_last_button_corner_radius();
        };

        void push_button_border_thickness(float thickness) override {
            m_buttonBorderThicknessStack.push_back(std::max(0.0f, thickness));
        };

        void pop_button_border_thickness() override {
            restore_last_button_border_thickness();
        };

        void push_button_padding(const Spherical::UIVec2& padding) override {
            m_buttonPaddingStack.push_back(ToNkVec2({std::max(0.0f, padding.x), std::max(0.0f, padding.y)}));
        };

        void pop_button_padding() override {
            restore_last_button_padding();
        };

        void push_button_style(Spherical::ButtonStyle style) override {
            m_buttonStyleStack.push_back(style);
        };

        void pop_button_style() override {
            restore_last_button_style();
        };

        void push_button_highlight_color(const Spherical::UIColor& color) override {
            m_buttonHighlightColorStack.push_back(ToNkColor(color));
        };

        void pop_button_highlight_color() override {
            restore_last_button_highlight_color();
        };

        void push_button_shadow_color(const Spherical::UIColor& color) override {
            m_buttonShadowColorStack.push_back(ToNkColor(color));
        };

        void pop_button_shadow_color() override {
            restore_last_button_shadow_color();
        };

        void push_button_border_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_buttonBorderColorStack.push_back(m_ctx->style.button.border_color);
            m_ctx->style.button.border_color = ToNkColor(color);
        };

        void pop_button_border_color() override {
            restore_last_button_border_color();
        };

        void push_button_text_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_buttonTextColorStack.push_back({
                m_ctx->style.button.text_normal,
                m_ctx->style.button.text_hover,
                m_ctx->style.button.text_active
            });

            const nk_color nkColor = ToNkColor(color);
            m_ctx->style.button.text_normal = nkColor;
            m_ctx->style.button.text_hover = nkColor;
            m_ctx->style.button.text_active = nkColor;
        };

        void pop_button_text_color() override {
            restore_last_button_text_color();
        };

        void push_text_input_background_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_textInputBackgroundColorStack.push_back({
                m_ctx->style.edit.normal,
                m_ctx->style.edit.hover,
                m_ctx->style.edit.active
            });

            const nk_style_item styleItem = ToNkStyleItem(color);
            m_ctx->style.edit.normal = styleItem;
            m_ctx->style.edit.hover = styleItem;
            m_ctx->style.edit.active = styleItem;
        };

        void pop_text_input_background_color() override {
            restore_last_text_input_background_color();
        };

        void push_text_input_text_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_textInputTextColorStack.push_back({
                m_ctx->style.edit.cursor_normal,
                m_ctx->style.edit.cursor_hover,
                m_ctx->style.edit.cursor_text_normal,
                m_ctx->style.edit.cursor_text_hover,
                m_ctx->style.edit.text_normal,
                m_ctx->style.edit.text_hover,
                m_ctx->style.edit.text_active,
                m_ctx->style.edit.selected_text_normal,
                m_ctx->style.edit.selected_text_hover
            });

            const nk_color nkColor = ToNkColor(color);
            m_ctx->style.edit.cursor_text_normal = nkColor;
            m_ctx->style.edit.cursor_text_hover = nkColor;
            m_ctx->style.edit.text_normal = nkColor;
            m_ctx->style.edit.text_hover = nkColor;
            m_ctx->style.edit.text_active = nkColor;
            m_ctx->style.edit.selected_text_normal = nkColor;
            m_ctx->style.edit.selected_text_hover = nkColor;
        };

        void pop_text_input_text_color() override {
            restore_last_text_input_text_color();
        };
        
    };

    bool IsDeviceSuitable(VkPhysicalDevice device) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        return queueFamilyCount > 0;
    }

    bool SelectPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(g_backend.instance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(g_backend.instance, &deviceCount, devices.data());

        for (VkPhysicalDevice device : devices) {
            if (!IsDeviceSuitable(device)) {
                continue;
            }
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                g_backend.physicalDevice = device;
                return true;
            }
        }

        for (VkPhysicalDevice device : devices) {
            if (IsDeviceSuitable(device)) {
                g_backend.physicalDevice = device;
                return true;
            }
        }

        return false;
    }

    bool FindQueueFamily() {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(g_backend.physicalDevice, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(g_backend.physicalDevice, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if (!(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                continue;
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(g_backend.physicalDevice, i, g_backend.surface, &presentSupport);
            if (presentSupport == VK_TRUE) {
                g_backend.graphicsQueueIndex = i;
                return true;
            }
        }
        return false;
    }

    bool CreateLogicalDevice() {
        const float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = g_backend.graphicsQueueIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingSupport{};
        dynamicRenderingSupport.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;

        VkPhysicalDeviceFeatures2 queriedFeatures{};
        queriedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        queriedFeatures.pNext = &dynamicRenderingSupport;
        vkGetPhysicalDeviceFeatures2(g_backend.physicalDevice, &queriedFeatures);

        if (dynamicRenderingSupport.dynamicRendering != VK_TRUE) {
            return false;
        }

        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingEnabled{};
        dynamicRenderingEnabled.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        dynamicRenderingEnabled.dynamicRendering = VK_TRUE;

        const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &dynamicRenderingEnabled;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = deviceExtensions;

        if (vkCreateDevice(g_backend.physicalDevice, &createInfo, nullptr, &g_backend.device) != VK_SUCCESS) {
            return false;
        }

        vkGetDeviceQueue(g_backend.device, g_backend.graphicsQueueIndex, 0, &g_backend.graphicsQueue);
        return true;
    }

    void DestroySwapchain() {
        if (g_backend.device == VK_NULL_HANDLE) {
            return;
        }

        for (VkImageView view : g_backend.swapchainImageViews) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(g_backend.device, view, nullptr);
            }
        }
        g_backend.swapchainImageViews.clear();
        g_backend.swapchainImages.clear();
        g_backend.swapchainImageLayouts.clear();

        if (g_backend.swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(g_backend.device, g_backend.swapchain, nullptr);
            g_backend.swapchain = VK_NULL_HANDLE;
        }
    }

    bool CreateSwapchain() {
        VkSurfaceCapabilitiesKHR capabilities{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_backend.physicalDevice, g_backend.surface, &capabilities) != VK_SUCCESS) {
            return false;
        }

        g_backend.swapchainExtent = capabilities.currentExtent;
        if (g_backend.swapchainExtent.width == UINT32_MAX) {
            int width = 0;
            int height = 0;
            SDL_GetWindowSizeInPixels(g_backend.window, &width, &height);
            g_backend.swapchainExtent.width = static_cast<uint32_t>(std::max(1, width));
            g_backend.swapchainExtent.height = static_cast<uint32_t>(std::max(1, height));
        }

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_backend.physicalDevice, g_backend.surface, &formatCount, nullptr);
        if (formatCount == 0) {
            return false;
        }

        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_backend.physicalDevice, g_backend.surface, &formatCount, formats.data());

        VkSurfaceFormatKHR surfaceFormat = formats[0];
        for (const VkSurfaceFormatKHR& candidate : formats) {
            if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM) {
                surfaceFormat = candidate;
                break;
            }
        }
        g_backend.swapchainFormat = surfaceFormat.format;

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_backend.physicalDevice, g_backend.surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_backend.physicalDevice, g_backend.surface, &presentModeCount, presentModes.data());

        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (g_backend.preferImmediatePresent) {
            for (VkPresentModeKHR mode : presentModes) {
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                    break;
                }
            }
        }

        uint32_t minImageCount = std::max(capabilities.minImageCount, 2u);
        if (capabilities.maxImageCount > 0) {
            minImageCount = std::min(minImageCount, capabilities.maxImageCount);
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = g_backend.surface;
        createInfo.minImageCount = minImageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = g_backend.swapchainExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(g_backend.device, &createInfo, nullptr, &g_backend.swapchain) != VK_SUCCESS) {
            return false;
        }

        uint32_t imageCount = 0;
        vkGetSwapchainImagesKHR(g_backend.device, g_backend.swapchain, &imageCount, nullptr);
        g_backend.swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(g_backend.device, g_backend.swapchain, &imageCount, g_backend.swapchainImages.data());
        g_backend.swapchainImageLayouts.assign(imageCount, VK_IMAGE_LAYOUT_UNDEFINED);

        g_backend.swapchainImageViews.resize(imageCount);
        for (size_t i = 0; i < imageCount; ++i) {
            VkImageViewCreateInfo viewCreateInfo{};
            viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCreateInfo.image = g_backend.swapchainImages[i];
            viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewCreateInfo.format = g_backend.swapchainFormat;
            viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCreateInfo.subresourceRange.baseMipLevel = 0;
            viewCreateInfo.subresourceRange.levelCount = 1;
            viewCreateInfo.subresourceRange.baseArrayLayer = 0;
            viewCreateInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(g_backend.device, &viewCreateInfo, nullptr, &g_backend.swapchainImageViews[i]) != VK_SUCCESS) {
                return false;
            }
        }

        return true;
    }

    bool RecreateSwapchain() {
        if (g_backend.device == VK_NULL_HANDLE) {
            return false;
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(g_backend.window, &width, &height);
        if (width <= 0 || height <= 0) {
            return false;
        }

        vkDeviceWaitIdle(g_backend.device);
        DestroySwapchain();
        return CreateSwapchain();
    }

    bool CreateCommandPoolAndBuffer() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = g_backend.graphicsQueueIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(g_backend.device, &poolInfo, nullptr, &g_backend.commandPool) != VK_SUCCESS) {
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = g_backend.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(g_backend.device, &allocInfo, &g_backend.commandBuffer) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

    bool CreateSyncPrimitives() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateSemaphore(g_backend.device, &semaphoreInfo, nullptr, &g_backend.imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(g_backend.device, &semaphoreInfo, nullptr, &g_backend.renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(g_backend.device, &fenceInfo, nullptr, &g_backend.inFlightFence) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

    bool BeginFrameCommandBuffer() {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(g_backend.commandBuffer, &beginInfo) != VK_SUCCESS) {
            return false;
        }

        VkImageMemoryBarrier toColorAttachment{};
        toColorAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toColorAttachment.oldLayout = g_backend.swapchainImageLayouts[g_backend.currentImageIndex];
        toColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.image = g_backend.swapchainImages[g_backend.currentImageIndex];
        toColorAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toColorAttachment.subresourceRange.baseMipLevel = 0;
        toColorAttachment.subresourceRange.levelCount = 1;
        toColorAttachment.subresourceRange.baseArrayLayer = 0;
        toColorAttachment.subresourceRange.layerCount = 1;
        toColorAttachment.srcAccessMask = 0;
        toColorAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(
            g_backend.commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &toColorAttachment);

        g_backend.swapchainImageLayouts[g_backend.currentImageIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        return true;
    }

    bool EndFrameCommandBuffer() {
        VkImageMemoryBarrier toPresent{};
        toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toPresent.oldLayout = g_backend.swapchainImageLayouts[g_backend.currentImageIndex];
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = g_backend.swapchainImages[g_backend.currentImageIndex];
        toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toPresent.subresourceRange.baseMipLevel = 0;
        toPresent.subresourceRange.levelCount = 1;
        toPresent.subresourceRange.baseArrayLayer = 0;
        toPresent.subresourceRange.layerCount = 1;
        toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.dstAccessMask = 0;

        vkCmdPipelineBarrier(
            g_backend.commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &toPresent);

        g_backend.swapchainImageLayouts[g_backend.currentImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        return vkEndCommandBuffer(g_backend.commandBuffer) == VK_SUCCESS;
    }

    bool PresentFrame() {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &g_backend.renderFinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &g_backend.swapchain;
        presentInfo.pImageIndices = &g_backend.currentImageIndex;

        const VkResult result = vkQueuePresentKHR(g_backend.graphicsQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            return RecreateSwapchain();
        }
        return result == VK_SUCCESS;
    }

    bool InitializeBackend(const Spherical::SphericalInitInfo& info) {
        if (g_backend.initialized) {
            return true;
        }

        g_backend.window = info.window;
        g_backend.preferImmediatePresent = info.preferImmediatePresent;

        SDL_Vulkan_LoadLibrary(nullptr);

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "SPHERICAL";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "SPHERICAL";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;

        uint32_t extensionCount = 0;
        const char* const* extensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        if (extensionNames == nullptr || extensionCount == 0) {
            return false;
        }

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = extensionNames;

        if (vkCreateInstance(&createInfo, nullptr, &g_backend.instance) != VK_SUCCESS) {
            return false;
        }

        if (!SDL_Vulkan_CreateSurface(g_backend.window, g_backend.instance, nullptr, &g_backend.surface)) {
            return false;
        }

        if (!SelectPhysicalDevice()) {
            return false;
        }

        if (!FindQueueFamily()) {
            return false;
        }

        if (!CreateLogicalDevice()) {
            return false;
        }

        if (!CreateSwapchain()) {
            return false;
        }

        if (!CreateCommandPoolAndBuffer()) {
            return false;
        }

        if (!CreateSyncPrimitives()) {
            return false;
        }

        g_backend.initialized = true;
        return true;
    }

    void ShutdownBackend() {
        if (g_backend.device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(g_backend.device);
        }

        if (g_backend.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(g_backend.device, g_backend.inFlightFence, nullptr);
            g_backend.inFlightFence = VK_NULL_HANDLE;
        }
        if (g_backend.renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(g_backend.device, g_backend.renderFinishedSemaphore, nullptr);
            g_backend.renderFinishedSemaphore = VK_NULL_HANDLE;
        }
        if (g_backend.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(g_backend.device, g_backend.imageAvailableSemaphore, nullptr);
            g_backend.imageAvailableSemaphore = VK_NULL_HANDLE;
        }

        DestroySwapchain();

        if (g_backend.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(g_backend.device, g_backend.commandPool, nullptr);
            g_backend.commandPool = VK_NULL_HANDLE;
            g_backend.commandBuffer = VK_NULL_HANDLE;
        }

        if (g_backend.device != VK_NULL_HANDLE) {
            vkDestroyDevice(g_backend.device, nullptr);
            g_backend.device = VK_NULL_HANDLE;
        }

        if (g_backend.surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(g_backend.instance, g_backend.surface, nullptr);
            g_backend.surface = VK_NULL_HANDLE;
        }

        if (g_backend.instance != VK_NULL_HANDLE) {
            vkDestroyInstance(g_backend.instance, nullptr);
            g_backend.instance = VK_NULL_HANDLE;
        }

        g_backend = {};
    }
}

namespace Spherical {
    static nk_context ctx = {};
    static bool initialized = false;
    static std::vector<unsigned char> nk_buffer_storage;
    static std::vector<unsigned char> nk_cmd_buffer_storage;

    static nk_user_font s_fallbackFont = {};
    static float FallbackFontWidth(nk_handle /*handle*/, float height, const char* /*text*/, int len) {
        return static_cast<float>(len) * (height * 0.54f);
    }

    namespace UIState {
        static float colorR = 0.9f;
        static float colorG = 0.2f;
        static float colorB = 0.2f;
        static int clickCounter = 0;
        static char textInput[65] = "Hello, World!";
        static int mouseX = 0;
        static int mouseY = 0;
        static double frameTime = 0.0;
        static std::chrono::high_resolution_clock::time_point lastFrameTime;
        static std::array<double, 60> frameTimes = {};
        static size_t frameIndex = 0;

        static bool isLoadingProject = false;
        static int projectsLoaded = 0;
        static std::chrono::high_resolution_clock::time_point loadStartTime;

        double GetFPS() {
            double totalMs = 0;
            for (double t : frameTimes) {
                totalMs += t;
            }
            double avgMs = totalMs / frameTimes.size();
            return avgMs > 0 ? 1000.0 / avgMs : 0.0;
        }

        void UpdateFrameTime() {
            const auto now = std::chrono::high_resolution_clock::now();
            if (lastFrameTime.time_since_epoch().count() == 0) {
                lastFrameTime = now;
                return;
            }

            frameTime = std::chrono::duration<double, std::milli>(now - lastFrameTime).count();
            lastFrameTime = now;
            frameTimes[frameIndex] = frameTime;
            frameIndex = (frameIndex + 1) % frameTimes.size();
        }
    }

    static void RenderUIToCommandBuffer(VkCommandBuffer cmd) {
        const VkExtent2D framebufferExtent = VulkanRenderer::GetFramebufferExtent();
        const float width = static_cast<float>(framebufferExtent.width);
        const float height = static_cast<float>(framebufferExtent.height);
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }

        // Call the registered UI build callback, or provide a default fallback
        if (g_uiBuildCallback) {
            UIPainterImpl painter(&ctx, framebufferExtent);
            g_uiBuildCallback(painter);
        }

        SyncWindowTextInputState(&ctx);

        void* vertPtr = nullptr;
        void* indexPtr = nullptr;
        size_t vertCapacity = 0;
        size_t indexCapacity = 0;

        if (!VulkanRenderer::MapVertexBuffer(&vertPtr, &vertCapacity)) {
            nk_clear(&ctx);
            return;
        }
        if (!VulkanRenderer::MapIndexBuffer(&indexPtr, &indexCapacity)) {
            VulkanRenderer::UnmapBuffers();
            nk_clear(&ctx);
            return;
        }

        nk_buffer cmds{};
        nk_buffer verts{};
        nk_buffer idxs{};
        nk_buffer_init_fixed(&cmds, nk_cmd_buffer_storage.data(), nk_cmd_buffer_storage.size());
        nk_buffer_init_fixed(&verts, vertPtr, vertCapacity);
        nk_buffer_init_fixed(&idxs, indexPtr, indexCapacity);

        static const nk_draw_vertex_layout_element vertexLayout[] = {
            {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct SphericalNkVertex, position)},
            {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct SphericalNkVertex, uv)},
            {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct SphericalNkVertex, col)},
            {NK_VERTEX_LAYOUT_END}
        };

        nk_draw_null_texture nullTexture{};
        nullTexture.texture = VulkanRenderer::GetNullTexture();
        nullTexture.uv = nk_vec2(0.5f, 0.5f);

        nk_convert_config config{};
        config.global_alpha = 1.0f;
        config.shape_AA = NK_ANTI_ALIASING_ON;
        config.line_AA = NK_ANTI_ALIASING_ON;
        config.arc_segment_count = 22;
        config.circle_segment_count = 22;
        config.curve_segment_count = 22;
        config.vertex_layout = vertexLayout;
        config.vertex_size = sizeof(SphericalNkVertex);
        config.vertex_alignment = NK_ALIGNOF(struct SphericalNkVertex);
        config.tex_null = nullTexture;

        const nk_flags convertResult = nk_convert(&ctx, &cmds, &verts, &idxs, &config);

        VulkanRenderer::UnmapBuffers();
        if (convertResult != NK_CONVERT_SUCCESS) {
            nk_clear(&ctx);
            return;
        }

        float proj[16];
        const float l = 0.0f;
        const float r = width;
        const float t = 0.0f;
        const float b = height;
        std::memset(proj, 0, sizeof(proj));
        proj[0] = 2.0f / (r - l);
        proj[5] = 2.0f / (b - t);
        proj[10] = -1.0f;
        proj[12] = -(r + l) / (r - l);
        proj[13] = -(b + t) / (b - t);
        proj[15] = 1.0f;

        VulkanRenderer::BeginUIPass(cmd, VK_NULL_HANDLE, proj);

        uint32_t indexOffset = 0;
        const nk_draw_command* drawCmd = nullptr;
        nk_draw_foreach(drawCmd, &ctx, &cmds) {
            if (drawCmd->elem_count == 0) {
                continue;
            }

            int scissorX = static_cast<int>(drawCmd->clip_rect.x);
            int scissorY = static_cast<int>(drawCmd->clip_rect.y);
            int scissorW = static_cast<int>(drawCmd->clip_rect.w);
            int scissorH = static_cast<int>(drawCmd->clip_rect.h);

            scissorX = std::max(0, scissorX);
            scissorY = std::max(0, scissorY);
            scissorW = std::min(scissorW, static_cast<int>(width) - scissorX);
            scissorH = std::min(scissorH, static_cast<int>(height) - scissorY);

            if (scissorW > 0 && scissorH > 0) {
                VulkanRenderer::DrawUICommand(cmd, drawCmd->texture, drawCmd->elem_count, indexOffset,
                                              scissorX, scissorY, scissorW, scissorH);
            }

            indexOffset += drawCmd->elem_count;
        }

        VulkanRenderer::EndUIPass(cmd);
        nk_clear(&ctx);
    }

    bool Init(const SphericalInitInfo& info) {
        if (initialized || info.window == nullptr) {
            return false;
        }

        if (!InitializeBackend(info)) {
            ShutdownBackend();
            return false;
        }

        VulkanRenderer::RendererInitInfo rendererInfo{};
        rendererInfo.device = g_backend.device;
        rendererInfo.physicalDevice = g_backend.physicalDevice;
        rendererInfo.graphicsQueue = g_backend.graphicsQueue;
        rendererInfo.commandPool = g_backend.commandPool;
        rendererInfo.colorAttachmentFormat = g_backend.swapchainFormat;
        rendererInfo.colorAttachmentView = g_backend.swapchainImageViews.empty() ? VK_NULL_HANDLE : g_backend.swapchainImageViews[0];
        rendererInfo.framebufferExtent = g_backend.swapchainExtent;

        if (!VulkanRenderer::Init(rendererInfo)) {
            ShutdownBackend();
            return false;
        }

        std::string resolvedFontPath;
        if (info.fontPath != nullptr && info.fontPath[0] != '\0') {
            resolvedFontPath = info.fontPath;
        } 

        const char* fontPath = resolvedFontPath.empty() ? nullptr : resolvedFontPath.c_str();
        const float uiScale = ResolveUiScale(info, g_backend.window);
        if (!FontRenderer::Init(g_backend.device, g_backend.physicalDevice,
                                g_backend.graphicsQueue, g_backend.commandPool,
                                fontPath, uiScale, info.fontRenderMode)) {
            VulkanRenderer::Shutdown();
            ShutdownBackend();
            return false;
        }

        const VkImageView atlasView = FontRenderer::GetAtlasImageView();
        if (atlasView != VK_NULL_HANDLE) {
            VulkanRenderer::UpdateFontTexture(atlasView);
        }

        static const size_t MAX_NUKLEAR_MEMORY = 16 * 1024 * 1024;
        static const size_t MAX_NUKLEAR_DRAW_COMMAND_MEMORY = 4 * 1024 * 1024;
        if (nk_buffer_storage.size() != MAX_NUKLEAR_MEMORY) {
            nk_buffer_storage.resize(MAX_NUKLEAR_MEMORY);
        }
        if (nk_cmd_buffer_storage.size() != MAX_NUKLEAR_DRAW_COMMAND_MEMORY) {
            nk_cmd_buffer_storage.resize(MAX_NUKLEAR_DRAW_COMMAND_MEMORY);
        }

        s_fallbackFont.height = std::ceil(12.0f * uiScale);
        s_fallbackFont.width = FallbackFontWidth;
        s_fallbackFont.userdata = nk_handle_ptr(nullptr);

        nk_user_font* fontToUse = FontRenderer::GetFontHandle(FontStyle::Regular);
        if (fontToUse == nullptr) {
            fontToUse = &s_fallbackFont;
        }

        nk_init_fixed(&ctx, nk_buffer_storage.data(), nk_buffer_storage.size(), fontToUse);
        if (g_backend.window != nullptr && SDL_TextInputActive(g_backend.window)) {
            SDL_StopTextInput(g_backend.window);
        }
        g_textInputWasActive = false;
        UIState::lastFrameTime = std::chrono::high_resolution_clock::now();

        TaskRunner::Init();
        initialized = true;
        return true;
    }

    void NewFrame() {
        if (!initialized) {
            return;
        }

        UIState::UpdateFrameTime();
        nk_input_begin(&ctx);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_MOUSE_MOTION: {
                    const int x = static_cast<int>(event.motion.x);
                    const int y = static_cast<int>(event.motion.y);
                    UIState::mouseX = x;
                    UIState::mouseY = y;
                    nk_input_motion(&ctx, x, y);
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    const int x = static_cast<int>(event.button.x);
                    const int y = static_cast<int>(event.button.y);
                        
                    int button = NK_BUTTON_LEFT;
                    if (event.button.button == SDL_BUTTON_MIDDLE) {
                        button = NK_BUTTON_MIDDLE;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        button = NK_BUTTON_RIGHT;
                    }
                        
                    const bool isDown = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);    
                    nk_input_button(&ctx, static_cast<nk_buttons>(button), x, y, isDown);
                    // SDK-side fix: when the left button is pressed, make Nuklear compute motion
                    // deltas relative to the click position by setting input.prev to clicked_pos.
                    // This gives absolute drag movement and mitigates scrollbar cursor lag
                    //if (isDown && button == NK_BUTTON_LEFT) {
                        //ctx.input.mouse.buttons is accessible here; set prev to clicked_pos
                        //ctx.input.mouse.prev.x = ctx.input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos.x;
                        //ctx.input.mouse.prev.y = ctx.input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos.y;
                    //}
                        
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL: {
                    if (event.wheel.y != 0) {
                        nk_input_scroll(&ctx, nk_vec2(0, event.wheel.y * 5.0f));
                    }
                    if (event.wheel.x != 0) {
                        nk_input_scroll(&ctx, nk_vec2(event.wheel.x * 5.0f, 0));
                    }
                    break;
                }
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: {
                    const bool isDown = (event.type == SDL_EVENT_KEY_DOWN);
                    if (event.key.key == SDLK_LSHIFT || event.key.key == SDLK_RSHIFT) {
                        nk_input_key(&ctx, NK_KEY_SHIFT, isDown);
                    } else if (event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL) {
                        nk_input_key(&ctx, NK_KEY_CTRL, isDown);
                    } else if (event.key.key == SDLK_DELETE) {
                        nk_input_key(&ctx, NK_KEY_DEL, isDown);
                    } else if (event.key.key == SDLK_RETURN) {
                        nk_input_key(&ctx, NK_KEY_ENTER, isDown);
                    } else if (event.key.key == SDLK_TAB) {
                        nk_input_key(&ctx, NK_KEY_TAB, isDown);
                    } else if (event.key.key == SDLK_BACKSPACE) {
                        nk_input_key(&ctx, NK_KEY_BACKSPACE, isDown);
                    } else if (event.key.key == SDLK_UP) {
                        nk_input_key(&ctx, NK_KEY_UP, isDown);
                    } else if (event.key.key == SDLK_DOWN) {
                        nk_input_key(&ctx, NK_KEY_DOWN, isDown);
                    } else if (event.key.key == SDLK_LEFT) {
                        nk_input_key(&ctx, NK_KEY_LEFT, isDown);
                    } else if (event.key.key == SDLK_RIGHT) {
                        nk_input_key(&ctx, NK_KEY_RIGHT, isDown);
                    } else if (event.key.key == SDLK_HOME) {
                        nk_input_key(&ctx, NK_KEY_TEXT_START, isDown);
                    } else if (event.key.key == SDLK_END) {
                        nk_input_key(&ctx, NK_KEY_TEXT_END, isDown);
                    }
                    break;
                }
                case SDL_EVENT_TEXT_INPUT: {
                    nk_glyph glyph;
                    std::memset(glyph, 0, sizeof(glyph));
                    std::strncpy(reinterpret_cast<char*>(glyph), event.text.text, NK_UTF_SIZE - 1);
                    nk_input_glyph(&ctx, glyph);
                    break;
                }
                default:
                    break;
            }
        }

        nk_input_end(&ctx);
        TaskRunner::Poll();
    }

    void Render() {
        if (!initialized || !g_backend.initialized) {
            return;
        }

        vkWaitForFences(g_backend.device, 1, &g_backend.inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(g_backend.device, 1, &g_backend.inFlightFence);

        const VkResult acquireResult = vkAcquireNextImageKHR(
            g_backend.device,
            g_backend.swapchain,
            UINT64_MAX,
            g_backend.imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &g_backend.currentImageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
            RecreateSwapchain();
            return;
        }
        if (acquireResult != VK_SUCCESS) {
            return;
        }

        vkResetCommandBuffer(g_backend.commandBuffer, 0);
        if (!BeginFrameCommandBuffer()) {
            return;
        }

        VulkanRenderer::SetRenderTarget(g_backend.swapchainImageViews[g_backend.currentImageIndex], g_backend.swapchainExtent);
        RenderUIToCommandBuffer(g_backend.commandBuffer);

        if (!EndFrameCommandBuffer()) {
            return;
        }

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &g_backend.imageAvailableSemaphore;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &g_backend.commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &g_backend.renderFinishedSemaphore;

        if (vkQueueSubmit(g_backend.graphicsQueue, 1, &submitInfo, g_backend.inFlightFence) != VK_SUCCESS) {
            return;
        }

        PresentFrame();
    }

    void Shutdown() {
        if (initialized) {
            nk_clear(&ctx);
        }

        TaskRunner::Shutdown();
        if (g_backend.window != nullptr && SDL_TextInputActive(g_backend.window)) {
            SDL_StopTextInput(g_backend.window);
        }
        g_textInputWasActive = false;
        FontRenderer::Shutdown();
        VulkanRenderer::Shutdown();
        ShutdownBackend();
        g_uiBuildCallback = nullptr;  // Release lambda captures and prevent stale callbacks on re-init
        initialized = false;
    }

    void RegisterUI(const UIBuildFn& callback) {
        g_uiBuildCallback = callback;
    }
}
