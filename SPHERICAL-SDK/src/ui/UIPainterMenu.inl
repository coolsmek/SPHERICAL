#include <unordered_map>
#include <string>

namespace {
    std::unordered_map<std::string, float> g_menuWidths;
    std::string g_currentMenuLabel;
}

bool UIPainterImpl::begin_dropdown_menu(const char* label) {
    if (m_ctx == nullptr) {
        return false;
    }
    
    g_currentMenuLabel = label;
    float width = 150.0f; // Default width
    if (g_menuWidths.find(label) != g_menuWidths.end()) {
        width = g_menuWidths[label];
    }
    
    // Style the dropdown popup itself
    nk_style_push_color(m_ctx, &m_ctx->style.window.menu_border_color, nk_rgb(0, 0, 0));
    nk_style_push_color(m_ctx, &m_ctx->style.window.popup_border_color, nk_rgb(0, 0, 0));
    nk_style_push_float(m_ctx, &m_ctx->style.window.popup_border, 1.0f);
    nk_style_push_vec2(m_ctx, &m_ctx->style.window.popup_padding, nk_vec2(0.0f, 0.0f));
    
    // Push the button layout for this dropdown in the menubar row
    float btnWidth = 60.0f;
    const struct nk_user_font* font = m_ctx->style.font;
    if (font) {
        btnWidth = font->width(font->userdata, font->height, label, nk_strlen(label)) + 20.0f;
    }
    nk_layout_row_push(m_ctx, btnWidth);
    
    // Make items sit exactly touching each other with no spacing
    nk_style_push_vec2(m_ctx, &m_ctx->style.window.spacing, nk_vec2(0.0f, 0.0f));

    bool result = nk_menu_begin_label(m_ctx, label, NK_TEXT_LEFT, nk_vec2(width, 400.0f));
    
    if (result) {
        // Reset tracked width for this frame to track newly submitted items
        g_menuWidths[label] = 50.0f; 
        
        // Push borderless/flush style for items inside
        nk_style_push_vec2(m_ctx, &m_ctx->style.menu_button.padding, nk_vec2(10.0f, 5.0f));
    } else {
        // Pop spacing if the menu didn't open (so we don't leak it)
        nk_style_pop_vec2(m_ctx);
        nk_style_pop_vec2(m_ctx);
        nk_style_pop_float(m_ctx);
        nk_style_pop_color(m_ctx);
        nk_style_pop_color(m_ctx);
    }
    
    return result;
}

void UIPainterImpl::end_dropdown_menu() {
    if (m_ctx == nullptr) return;
    
    nk_style_pop_vec2(m_ctx); // pop menu_button padding
    nk_menu_end(m_ctx);
    
    nk_style_pop_vec2(m_ctx); // pop window spacing
    nk_style_pop_vec2(m_ctx); // pop popup_padding
    nk_style_pop_float(m_ctx); // pop popup_border
    nk_style_pop_color(m_ctx); // pop popup_border_color
    nk_style_pop_color(m_ctx); // pop menu_border_color
    
    g_currentMenuLabel = "";
}

bool UIPainterImpl::menu_item(const char* label) {
    if (m_ctx == nullptr) return false;
    
    // Calculate width to auto-size the menu next frame
    const struct nk_user_font* font = m_ctx->style.font;
    if (font) {
        float textWidth = font->width(font->userdata, font->height, label, nk_strlen(label));
        float totalWidth = textWidth + 30.0f; // Padding
        if (totalWidth > g_menuWidths[g_currentMenuLabel]) {
            g_menuWidths[g_currentMenuLabel] = totalWidth;
        }
    }
    
    nk_layout_row_dynamic(m_ctx, button_row_height(), 1);
    return nk_menu_item_label(m_ctx, label, NK_TEXT_LEFT) != 0;
}
