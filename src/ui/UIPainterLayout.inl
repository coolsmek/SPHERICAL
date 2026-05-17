Spherical::UIRect UIPainterImpl::get_current_panel_bounds() const {
    return m_currentPanelBounds;
}

Spherical::UIRect UIPainterImpl::get_current_panel_content_bounds() const {
    return m_currentPanelContentBounds;
}

bool UIPainterImpl::begin_panel_subsection(const char* title) {
    if (m_ctx == nullptr || m_activePanelTitle == nullptr || title == nullptr) {
        return false;
    }

    const bool parentVisible = std::all_of(
        m_panelSubsectionStack.begin(),
        m_panelSubsectionStack.end(),
        [](const PanelSubsectionFrameState& state) {
            return state.parentVisible && state.expanded;
        }
    );

    std::string hierarchyPath;
    if (!m_panelSubsectionStack.empty()) {
        hierarchyPath = m_panelSubsectionStack.back().hierarchyPath;
        hierarchyPath.push_back(kPanelSubsectionStatePathSeparator);
    }
    hierarchyPath += title;

    PanelSubsectionFrameState frameState{};
    frameState.hierarchyPath = hierarchyPath;
    frameState.parentVisible = parentVisible;

    if (!parentVisible) {
        frameState.expanded = false;
        m_panelSubsectionStack.push_back(frameState);
        return false;
    }

    std::string stateKey = m_activePanelTitle;
    stateKey.push_back(kPanelSubsectionStatePanelSeparator);
    stateKey += hierarchyPath;

    PanelSubsectionPersistentState& persistentState = g_panelSubsectionStates[stateKey];
    if (!persistentState.initialized) {
        persistentState.initialized = true;
        persistentState.expanded = true;
    }

    nk_layout_row_dynamic(m_ctx, subsection_header_row_height(), 1);

    if (m_ctx->current != nullptr && m_ctx->current->layout != nullptr) {
        nk_window* win = m_ctx->current;
        nk_panel* layout = win->layout;

        struct nk_rect bounds{};
        const nk_widget_layout_states widgetState = nk_widget(&bounds, m_ctx);
        if (widgetState) {
            const bool isReadOnly = (widgetState == NK_WIDGET_DISABLED) || ((layout->flags & NK_WINDOW_ROM) != 0);
            const float indent = static_cast<float>(m_panelSubsectionStack.size()) * subsection_indent_step();
            bounds.x += indent;
            bounds.w = std::max(1.0f, bounds.w - indent);

            const bool hoveredHeader = IsMouseInsideRect(m_ctx, bounds);
            const nk_color backgroundColor = hoveredHeader
                ? LightenColor(m_ctx->style.window.background, 0.05f)
                : BlendColor(m_ctx->style.window.background, m_ctx->style.window.border_color, 0.14f);
            const nk_color borderColor = hoveredHeader
                ? LightenColor(m_ctx->style.window.border_color, 0.08f)
                : m_ctx->style.window.border_color;

            nk_fill_rect(&win->buffer, bounds, 4.0f, backgroundColor);
            nk_stroke_rect(&win->buffer, bounds, 4.0f, 1.0f, borderColor);

            const float buttonSize = std::max(12.0f, std::min(bounds.h - 6.0f, current_font_height() * 1.15f));
            const struct nk_rect buttonRect = nk_rect(
                bounds.x + 6.0f,
                bounds.y + std::max(2.0f, (bounds.h - buttonSize) * 0.5f),
                buttonSize,
                buttonSize
            );
            const bool hoveredButton = IsMouseInsideRect(m_ctx, buttonRect);
            const bool activated = !isReadOnly && WasLeftMousePressed(m_ctx) && (hoveredHeader || hoveredButton);
            if (activated) {
                persistentState.expanded = !persistentState.expanded;
                m_ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
            }

            const nk_color buttonColor = hoveredButton
                ? LightenColor(backgroundColor, 0.12f)
                : LightenColor(backgroundColor, 0.06f);
            const nk_color chevronColor = hoveredHeader
                ? LightenColor(m_ctx->style.text.color, 0.10f)
                : m_ctx->style.text.color;

            nk_fill_rect(&win->buffer, buttonRect, 3.0f, buttonColor);
            nk_stroke_rect(&win->buffer, buttonRect, 3.0f, 1.0f, borderColor);

            const float iconPadding = std::max(2.0f, buttonRect.w * 0.22f);
            const float left = buttonRect.x + iconPadding;
            const float right = buttonRect.x + buttonRect.w - iconPadding;
            const float top = buttonRect.y + iconPadding;
            const float bottom = buttonRect.y + buttonRect.h - iconPadding;
            const float centerX = buttonRect.x + buttonRect.w * 0.5f;
            const float centerY = buttonRect.y + buttonRect.h * 0.5f;

            if (persistentState.expanded) {
                nk_stroke_line(&win->buffer, left, top, centerX, bottom, 2.0f, chevronColor);
                nk_stroke_line(&win->buffer, centerX, bottom, right, top, 2.0f, chevronColor);
            } else {
                nk_stroke_line(&win->buffer, left, top, right, centerY, 2.0f, chevronColor);
                nk_stroke_line(&win->buffer, left, bottom, right, centerY, 2.0f, chevronColor);
            }

            const nk_user_font* textFont = m_ctx->style.font;
            if (textFont == nullptr) {
                textFont = Spherical::FontRenderer::GetFontHandle(Spherical::FontStyle::Regular);
            }

            if (textFont != nullptr) {
                const int titleLength = static_cast<int>(std::strlen(title));
                const float labelX = buttonRect.x + buttonRect.w + 8.0f;
                const float labelWidth = std::max(1.0f, (bounds.x + bounds.w) - labelX - 8.0f);
                const struct nk_rect labelBounds = nk_rect(
                    labelX,
                    bounds.y + std::max(0.0f, (bounds.h - textFont->height) * 0.5f),
                    labelWidth,
                    textFont->height
                );
                nk_draw_text(&win->buffer, labelBounds, title, titleLength, textFont, nk_rgba(0, 0, 0, 0), chevronColor);
            }
        }
    }

    frameState.expanded = persistentState.expanded;
    m_panelSubsectionStack.push_back(frameState);
    return persistentState.expanded;
}

void UIPainterImpl::end_panel_subsection() {
    if (!m_panelSubsectionStack.empty()) {
        m_panelSubsectionStack.pop_back();
    }
}

void UIPainterImpl::label(const char* text) {
    nk_layout_row_dynamic(m_ctx, label_row_height(), 1);
    nk_label(m_ctx, text, NK_TEXT_LEFT);
}

void UIPainterImpl::spacing() {
    nk_layout_row_dynamic(m_ctx, spacing_row_height(), 1);
    nk_spacing(m_ctx, 1);
}

