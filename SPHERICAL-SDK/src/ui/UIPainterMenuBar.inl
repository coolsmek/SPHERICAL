bool UIPainterImpl::begin_menu_bar() {
    if (m_ctx == nullptr) {
        return false;
    }

    // Set panel bounds for a fixed menu bar at the top
    const float menuBarHeight = 26.0f; // Fixed height
    struct nk_rect bounds = nk_rect(0.0f, 0.0f, static_cast<float>(m_framebufferExtent.width), menuBarHeight);

    // Push style changes for the menu bar to make it flat and remove padding/borders
    nk_style_push_vec2(m_ctx, &m_ctx->style.window.padding, nk_vec2(0.0f, 0.0f));
    nk_style_push_float(m_ctx, &m_ctx->style.window.border, 0.0f);
    nk_style_push_color(m_ctx, &m_ctx->style.window.background, nk_rgb(20, 20, 38));
    nk_style_push_style_item(m_ctx, &m_ctx->style.window.fixed_background, nk_style_item_color(nk_rgb(20, 20, 20)));

    nk_flags panelFlags = NK_WINDOW_NO_SCROLLBAR; // Removed NK_WINDOW_BACKGROUND so it's not hidden behind everything

    // Force bounds every frame to handle window resizing
    nk_window_set_bounds(m_ctx, "MenuBar", bounds);

    bool result = nk_begin(
        m_ctx,
        "MenuBar",
        bounds,
        panelFlags) != 0;

    // Push global button styles specifically for the menu bar
    Spherical::UIColor menuBarColor = { 20.0f/255.0f, 20.0f/255.0f, 38.0f/255.0f, 1.0f };
    Spherical::UIColor hoverColor = { 40.0f/255.0f, 40.0f/255.0f, 70.0f/255.0f, 1.0f };
    Spherical::UIColor activeColor = { 60.0f/255.0f, 60.0f/255.0f, 90.0f/255.0f, 1.0f };

    push_button_background_color(menuBarColor);
    push_button_hover_background_color(hoverColor);
    push_button_clicked_background_color(activeColor);
    push_button_border_thickness(0.0f);
    push_button_corner_radius(0.0f);
    push_button_height(menuBarHeight - 2.0f);

        
    if (result) {
        nk_menubar_begin(m_ctx);
        nk_layout_row_begin(m_ctx, NK_STATIC, menuBarHeight, 15);

        // Draw 1px black bottom border
        nk_command_buffer* canvas = nk_window_get_canvas(m_ctx);
        if (canvas) {
            nk_stroke_line(canvas, bounds.x, bounds.y + bounds.h - 1.0f, bounds.x + bounds.w, bounds.y + bounds.h - 1.0f, 1.0f, nk_rgb(0, 0, 0));
        }
    }
    
    return result;
}

void UIPainterImpl::end_menu_bar() {
    nk_layout_row_end(m_ctx);
    nk_menubar_end(m_ctx);

    // Pop the button styles we pushed
    pop_button_height();
    pop_button_corner_radius();
    pop_button_border_thickness();
    pop_button_clicked_background_color();
    pop_button_hover_background_color();
    pop_button_background_color();

    nk_end(m_ctx);
    
    // Pop the styles we pushed
    nk_style_pop_style_item(m_ctx);
    nk_style_pop_color(m_ctx);
    nk_style_pop_float(m_ctx);
    nk_style_pop_vec2(m_ctx);
}
