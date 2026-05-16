void UIPainterImpl::slider_float(const char* label, float* value, float min, float max, float step) {
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

