bool UIPainterImpl::begin_panel(const char* title, int x, int y, int width, int height) {
    if (m_ctx == nullptr || title == nullptr) {
        m_currentPanelBounds = {};
        m_currentPanelContentBounds = {};
        m_activePanelTitle = nullptr;
        return false;
    }

    constexpr float kMinWidth = 240.0f;
    constexpr float kMinHeight = 180.0f;
    constexpr float kCornerHandleSize = 2.0f;
    constexpr float kEdgeHandleThickness = 2.0f;

    const float centerX = static_cast<float>(m_framebufferExtent.width) * 0.5f;
    const float centerY = static_cast<float>(m_framebufferExtent.height) * 0.5f;

    auto clamp_bounds = [&](struct nk_rect& bounds) {
        bounds.w = std::max(bounds.w, kMinWidth);
        bounds.h = std::max(bounds.h, kMinHeight);
    };

    auto bounds_from_state = [&](const PanelPersistentState& state) {
        struct nk_rect bounds = nk_rect(
            centerX + state.offsetFromCenterX,
            centerY + state.offsetFromCenterY,
            state.width,
            state.height
        );
        clamp_bounds(bounds);
        return bounds;
    };

    auto write_state_from_bounds = [&](PanelPersistentState& state, const struct nk_rect& bounds) {
        state.offsetFromCenterX = bounds.x - centerX;
        state.offsetFromCenterY = bounds.y - centerY;
        state.width = bounds.w;
        state.height = bounds.h;
    };

    PanelPersistentState& panelState = g_panelStates[title];
    if (!panelState.initialized) {
        panelState.width = std::max(kMinWidth, static_cast<float>(width));
        panelState.height = std::max(kMinHeight, static_cast<float>(height));
        panelState.offsetFromCenterX = static_cast<float>(x) - centerX;
        panelState.offsetFromCenterY = static_cast<float>(y) - centerY;
        panelState.initialized = true;
    }

    struct nk_rect panelBounds = bounds_from_state(panelState);

    if (g_panelDrag.active && g_panelDrag.windowTitle == title) {
        if (!IsLeftMouseDown(m_ctx)) {
            g_panelDrag = {};
        } else {
            const bool isResizeDrag = g_panelDrag.mode != PanelDragMode::Move && g_panelDrag.mode != PanelDragMode::None;
            const float mouseX = m_ctx->input.mouse.pos.x;
            const float mouseY = m_ctx->input.mouse.pos.y;
            const bool mouseInsideWindow = mouseX >= 0.0f && mouseX <= static_cast<float>(m_framebufferExtent.width) &&
                                           mouseY >= 0.0f && mouseY <= static_cast<float>(m_framebufferExtent.height);

            if (!isResizeDrag || mouseInsideWindow) {
                float dx = 0.0f;
                float dy = 0.0f;
                if (g_panelDrag.mode == PanelDragMode::Move) {
                    float globalX = 0.0f;
                    float globalY = 0.0f;
                    SDL_GetGlobalMouseState(&globalX, &globalY);
                    dx = globalX - g_panelDrag.globalMouseStartX;
                    dy = globalY - g_panelDrag.globalMouseStartY;
                } else {
                    dx = m_ctx->input.mouse.pos.x - g_panelDrag.mouseStartX;
                    dy = m_ctx->input.mouse.pos.y - g_panelDrag.mouseStartY;
                }

                struct nk_rect nextBounds = g_panelDrag.panelStartBounds;
                switch (g_panelDrag.mode) {
                    case PanelDragMode::Move:
                        nextBounds.x = g_panelDrag.panelStartBounds.x + dx;
                        nextBounds.y = g_panelDrag.panelStartBounds.y + dy;
                        break;
                    case PanelDragMode::ResizeTopLeft:
                        nextBounds.x = g_panelDrag.panelStartBounds.x + dx;
                        nextBounds.y = g_panelDrag.panelStartBounds.y + dy;
                        nextBounds.w = g_panelDrag.panelStartBounds.w - dx;
                        nextBounds.h = g_panelDrag.panelStartBounds.h - dy;
                        break;
                    case PanelDragMode::ResizeTopRight:
                        nextBounds.y = g_panelDrag.panelStartBounds.y + dy;
                        nextBounds.w = g_panelDrag.panelStartBounds.w + dx;
                        nextBounds.h = g_panelDrag.panelStartBounds.h - dy;
                        break;
                    case PanelDragMode::ResizeBottomLeft:
                        nextBounds.x = g_panelDrag.panelStartBounds.x + dx;
                        nextBounds.w = g_panelDrag.panelStartBounds.w - dx;
                        nextBounds.h = g_panelDrag.panelStartBounds.h + dy;
                        break;
                    case PanelDragMode::ResizeBottomRight:
                        nextBounds.w = g_panelDrag.panelStartBounds.w + dx;
                        nextBounds.h = g_panelDrag.panelStartBounds.h + dy;
                        break;
                    case PanelDragMode::ResizeTop:
                        nextBounds.y = g_panelDrag.panelStartBounds.y + dy;
                        nextBounds.h = g_panelDrag.panelStartBounds.h - dy;
                        break;
                    case PanelDragMode::ResizeRight:
                        nextBounds.w = g_panelDrag.panelStartBounds.w + dx;
                        break;
                    case PanelDragMode::ResizeBottom:
                        nextBounds.h = g_panelDrag.panelStartBounds.h + dy;
                        break;
                    case PanelDragMode::ResizeLeft:
                        nextBounds.x = g_panelDrag.panelStartBounds.x + dx;
                        nextBounds.w = g_panelDrag.panelStartBounds.w - dx;
                        break;
                    case PanelDragMode::None:
                    default:
                        break;
                }

                if (nextBounds.w < kMinWidth) {
                    if (g_panelDrag.mode == PanelDragMode::ResizeTopLeft || g_panelDrag.mode == PanelDragMode::ResizeBottomLeft ||
                        g_panelDrag.mode == PanelDragMode::ResizeLeft) {
                        nextBounds.x = g_panelDrag.panelStartBounds.x + (g_panelDrag.panelStartBounds.w - kMinWidth);
                    }
                    nextBounds.w = kMinWidth;
                }

                if (nextBounds.h < kMinHeight) {
                    if (g_panelDrag.mode == PanelDragMode::ResizeTopLeft || g_panelDrag.mode == PanelDragMode::ResizeTopRight ||
                        g_panelDrag.mode == PanelDragMode::ResizeTop) {
                        nextBounds.y = g_panelDrag.panelStartBounds.y + (g_panelDrag.panelStartBounds.h - kMinHeight);
                    }
                    nextBounds.h = kMinHeight;
                }

                clamp_bounds(nextBounds);
                panelBounds = nextBounds;
                write_state_from_bounds(panelState, panelBounds);
            }
        }
    }

    clamp_bounds(panelBounds);
    write_state_from_bounds(panelState, panelBounds);

    const nk_user_font* titleFont = Spherical::FontRenderer::GetFontHandle(Spherical::FontStyle::Title);
    if (titleFont == nullptr) {
        titleFont = (m_ctx != nullptr) ? m_ctx->style.font : nullptr;
    }
    const bool pushedTitleFont = (titleFont != nullptr);
    if (pushedTitleFont) {
        nk_style_push_font(m_ctx, titleFont);
    }

    const bool result = nk_begin(
        m_ctx,
        title,
        panelBounds,
        NK_WINDOW_BORDER | NK_WINDOW_TITLE) != 0;
    if (pushedTitleFont) {
        nk_style_pop_font(m_ctx);
    }

    if (result) {
        const struct nk_rect windowBounds = nk_window_get_bounds(m_ctx);
        const struct nk_rect contentRegion = nk_window_get_content_region(m_ctx);
        panelBounds = windowBounds;
        write_state_from_bounds(panelState, panelBounds);
        m_currentPanelBounds = {windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h};
        m_currentPanelContentBounds = {contentRegion.x, contentRegion.y, contentRegion.w, contentRegion.h};

        const float headerH = GetWindowHeaderHeight(m_ctx);
        const struct nk_rect headerRect = nk_rect(windowBounds.x, windowBounds.y, windowBounds.w, headerH);
        const struct nk_rect topLeft = nk_rect(windowBounds.x, windowBounds.y, kCornerHandleSize, kCornerHandleSize);
        const struct nk_rect topRight = nk_rect(windowBounds.x + windowBounds.w - kCornerHandleSize, windowBounds.y, kCornerHandleSize, kCornerHandleSize);
        const struct nk_rect bottomLeft = nk_rect(windowBounds.x, windowBounds.y + windowBounds.h - kCornerHandleSize, kCornerHandleSize, kCornerHandleSize);
        const struct nk_rect bottomRight = nk_rect(windowBounds.x + windowBounds.w - kCornerHandleSize, windowBounds.y + windowBounds.h - kCornerHandleSize, kCornerHandleSize, kCornerHandleSize);
        const struct nk_rect edgeTop = nk_rect(windowBounds.x + kCornerHandleSize, windowBounds.y, std::max(0.0f, windowBounds.w - 2.0f * kCornerHandleSize), kEdgeHandleThickness);
        const struct nk_rect edgeRight = nk_rect(windowBounds.x + windowBounds.w - kEdgeHandleThickness, windowBounds.y + kCornerHandleSize, kEdgeHandleThickness, std::max(0.0f, windowBounds.h - 2.0f * kCornerHandleSize));
        const struct nk_rect edgeBottom = nk_rect(windowBounds.x + kCornerHandleSize, windowBounds.y + windowBounds.h - kEdgeHandleThickness, std::max(0.0f, windowBounds.w - 2.0f * kCornerHandleSize), kEdgeHandleThickness);
        const struct nk_rect edgeLeft = nk_rect(windowBounds.x, windowBounds.y + kCornerHandleSize, kEdgeHandleThickness, std::max(0.0f, windowBounds.h - 2.0f * kCornerHandleSize));

        if (g_panelDrag.active && g_panelDrag.windowTitle == title) {
            switch (g_panelDrag.mode) {
                case PanelDragMode::ResizeTopLeft:
                case PanelDragMode::ResizeBottomRight:
                    RequestCursor(CursorRequest::ResizeNwse);
                    break;
                case PanelDragMode::ResizeTopRight:
                case PanelDragMode::ResizeBottomLeft:
                    RequestCursor(CursorRequest::ResizeNesw);
                    break;
                case PanelDragMode::ResizeTop:
                case PanelDragMode::ResizeBottom:
                    RequestCursor(CursorRequest::ResizeNs);
                    break;
                case PanelDragMode::ResizeLeft:
                case PanelDragMode::ResizeRight:
                    RequestCursor(CursorRequest::ResizeEw);
                    break;
                case PanelDragMode::None:
                case PanelDragMode::Move:
                default:
                    break;
            }
        } else {
            if (IsMouseInsideRect(m_ctx, topLeft) || IsMouseInsideRect(m_ctx, bottomRight)) {
                RequestCursor(CursorRequest::ResizeNwse);
            } else if (IsMouseInsideRect(m_ctx, topRight) || IsMouseInsideRect(m_ctx, bottomLeft)) {
                RequestCursor(CursorRequest::ResizeNesw);
            } else if (IsMouseInsideRect(m_ctx, edgeTop) || IsMouseInsideRect(m_ctx, edgeBottom)) {
                RequestCursor(CursorRequest::ResizeNs);
            } else if (IsMouseInsideRect(m_ctx, edgeLeft) || IsMouseInsideRect(m_ctx, edgeRight)) {
                RequestCursor(CursorRequest::ResizeEw);
            }
        }

        if (!g_panelDrag.active && WasLeftMousePressed(m_ctx)) {
            PanelDragMode startMode = PanelDragMode::None;
            if (IsMouseInsideRect(m_ctx, topLeft)) {
                startMode = PanelDragMode::ResizeTopLeft;
            } else if (IsMouseInsideRect(m_ctx, topRight)) {
                startMode = PanelDragMode::ResizeTopRight;
            } else if (IsMouseInsideRect(m_ctx, bottomLeft)) {
                startMode = PanelDragMode::ResizeBottomLeft;
            } else if (IsMouseInsideRect(m_ctx, bottomRight)) {
                startMode = PanelDragMode::ResizeBottomRight;
            } else if (IsMouseInsideRect(m_ctx, edgeTop)) {
                startMode = PanelDragMode::ResizeTop;
            } else if (IsMouseInsideRect(m_ctx, edgeRight)) {
                startMode = PanelDragMode::ResizeRight;
            } else if (IsMouseInsideRect(m_ctx, edgeBottom)) {
                startMode = PanelDragMode::ResizeBottom;
            } else if (IsMouseInsideRect(m_ctx, edgeLeft)) {
                startMode = PanelDragMode::ResizeLeft;
            } else if (IsMouseInsideRect(m_ctx, headerRect)) {
                startMode = PanelDragMode::Move;
            }

            if (startMode != PanelDragMode::None) {
                g_panelDrag.active = true;
                g_panelDrag.windowTitle = title;
                g_panelDrag.mode = startMode;
                g_panelDrag.mouseStartX = m_ctx->input.mouse.pos.x;
                g_panelDrag.mouseStartY = m_ctx->input.mouse.pos.y;
                g_panelDrag.panelStartBounds = panelBounds;
                float globalX = 0.0f;
                float globalY = 0.0f;
                SDL_GetGlobalMouseState(&globalX, &globalY);
                g_panelDrag.globalMouseStartX = globalX;
                g_panelDrag.globalMouseStartY = globalY;
                m_ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
            }
        }

        if (m_ctx->current != nullptr) {
            const nk_color grip = m_ctx->style.window.border_color;
            nk_command_buffer* buffer = &m_ctx->current->buffer;
            nk_fill_rect(buffer, topLeft, 0.0f, grip);
            nk_fill_rect(buffer, topRight, 0.0f, grip);
            nk_fill_rect(buffer, bottomLeft, 0.0f, grip);
            nk_fill_rect(buffer, bottomRight, 0.0f, grip);
        }

        apply_active_vertical_scrollbar_drag(title);
    } else {
        m_currentPanelBounds = {};
        m_currentPanelContentBounds = {};
        if (g_panelDrag.active && g_panelDrag.windowTitle == title && !IsLeftMouseDown(m_ctx)) {
            g_panelDrag = {};
        }
    }

    m_activePanelTitle = result ? title : nullptr;
    if (!result) {
        release_scrollbar_drag_if_needed(title);
    }

    return result;
}

void UIPainterImpl::end_panel() {
    if (m_activePanelTitle != nullptr) {
        // Refresh drag geometry and detect drag-start after content layout is known for this frame.
        refresh_vertical_scrollbar_drag_state(m_activePanelTitle);
    }
    nk_end(m_ctx);
    m_activePanelTitle = nullptr;
}

