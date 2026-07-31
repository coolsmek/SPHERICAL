bool UIPainterImpl::begin_panel(const char* title, int x, int y, int width, int height, Spherical::PanelFlags flags) {
    m_panelSubsectionStack.clear();

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

    auto clamp_bounds = [&](struct nk_rect& bounds, bool is_resizing = false) {
        bounds.w = std::max(bounds.w, kMinWidth);
        bounds.h = std::max(bounds.h, kMinHeight);

        if (g_workspaceBoundarySize.x > 0.0f && g_workspaceBoundarySize.y > 0.0f) {
            const float bx = centerX - (g_workspaceBoundarySize.x * 0.5f) + g_workspaceBoundaryOffset.x;
            const float by = centerY - (g_workspaceBoundarySize.y * 0.5f) - g_workspaceBoundaryOffset.y;
            const float bw = g_workspaceBoundarySize.x;
            const float bh = g_workspaceBoundarySize.y;

            if (bounds.w > bw) bounds.w = bw;
            if (bounds.h > bh) bounds.h = bh;

            if (is_resizing) {
                if (bounds.x < bx) {
                    bounds.w -= (bx - bounds.x);
                    bounds.x = bx;
                }
                if (bounds.y < by) {
                    bounds.h -= (by - bounds.y);
                    bounds.y = by;
                }
                if (bounds.x + bounds.w > bx + bw) {
                    bounds.w = (bx + bw) - bounds.x;
                }
                if (bounds.y + bounds.h > by + bh) {
                    bounds.h = (by + bh) - bounds.y;
                }
                // Enforce min size again after possible shrinkage
                bounds.w = std::max(bounds.w, kMinWidth);
                bounds.h = std::max(bounds.h, kMinHeight);
            } else {
                if (bounds.x < bx) bounds.x = bx;
                if (bounds.y < by) bounds.y = by;

                if (bounds.x + bounds.w > bx + bw) {
                    bounds.x = (bx + bw) - bounds.w;
                }
                if (bounds.y + bounds.h > by + bh) {
                    bounds.y = (by + bh) - bounds.h;
                }
            }
        }
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
        panelState.offsetFromCenterX = static_cast<float>(x) - (panelState.width * 0.5f);
        panelState.offsetFromCenterY = -static_cast<float>(y) - (panelState.height * 0.5f);
        panelState.initialWidth = panelState.width;
        panelState.initialHeight = panelState.height;
        panelState.initialOffsetFromCenterX = panelState.offsetFromCenterX;
        panelState.initialOffsetFromCenterY = panelState.offsetFromCenterY;
        panelState.initialized = true;
    }

    struct nk_rect panelBounds = bounds_from_state(panelState);

    // Check if this panel is docked in a workspace container
    bool isPanelDocked = false;
    bool disallowUndock = false;
    for (auto& [containerTitle, containerState] : g_workspaceContainerStates) {
        if (containerState.root == nullptr) {
            continue;
        }
        DockNode* leafNode = FindDockNodeByPanelTitle(containerState.root.get(), title);
        if (leafNode != nullptr) {
            // Panel is docked - inset its rect so splitter lines remain visible between docked panels.
            panelBounds = InsetDockedPanelRect(leafNode->computedRect);
            isPanelDocked = true;
            disallowUndock = containerState.disallowUndock;
            break;
        }
    }

    const bool isLocked = (static_cast<uint32_t>(flags) & static_cast<uint32_t>(Spherical::PanelFlags::Locked)) != 0;

    if (g_panelDrag.active && g_panelDrag.windowTitle == title) {
        if (isLocked || (isPanelDocked && g_panelDrag.mode != PanelDragMode::Move)) {
            g_panelDrag = {};
            SDL_CaptureMouse(false);
        }

        if (!IsLeftMouseDownAnywhere(m_ctx)) {
            // Mouse released - check for drop into workspace container
            if (g_panelDrag.mode == PanelDragMode::Move && g_dropTargetState.active) {
                auto it = g_workspaceContainerStates.find(g_dropTargetState.containerTitle);
                if (it != g_workspaceContainerStates.end()) {
                    if (it->second.root == nullptr) {
                        auto newLeaf = std::make_unique<DockNode>();
                        newLeaf->type = DockNode::Type::Leaf;
                        newLeaf->panelTitle = title;
                        it->second.root = std::move(newLeaf);
                        it->second.pendingSplitterReconcile = true;
                    } else if (g_dropTargetState.hoveredLeaf != nullptr) {
                        InsertDockNode(it->second.root, &it->second, g_dropTargetState.hoveredLeaf, title, g_dropTargetState.zone);
                    }
                }
            }
            g_dropTargetState = {};
            g_panelDrag = {};
            SDL_CaptureMouse(false);
        } else {
            // Drag is active - detect workspace container drops
            if (g_panelDrag.mode == PanelDragMode::Move && !isPanelDocked) {
                const float mouseX = m_ctx->input.mouse.pos.x;
                const float mouseY = m_ctx->input.mouse.pos.y;

                // Check all workspace containers for overlap
                g_dropTargetState.active = false;
                g_dropTargetState.hoveredLeaf = nullptr;
                for (auto& [containerName, containerState] : g_workspaceContainerStates) {
                    if (!containerState.initialized) {
                        continue;
                    }

                    struct nk_rect containerBounds = nk_rect(
                        centerX + containerState.offsetFromCenterX,
                        centerY + containerState.offsetFromCenterY,
                        containerState.width,
                        containerState.height
                    );

                    const float headerHeight = containerState.headerHeight > 0.0f
                        ? containerState.headerHeight
                        : GetWindowHeaderHeight(m_ctx);
                    struct nk_rect bodyRect = containerBounds;
                    bodyRect.y += headerHeight;
                    bodyRect.h = std::max(0.0f, bodyRect.h - headerHeight);

                    if (bodyRect.w <= 0.0f || bodyRect.h <= 0.0f ||
                        mouseX < bodyRect.x || mouseX > bodyRect.x + bodyRect.w ||
                        mouseY < bodyRect.y || mouseY > bodyRect.y + bodyRect.h) {
                        continue; // Not over this container
                    }

                    if (containerState.root == nullptr) {
                        g_dropTargetState.active = true;
                        g_dropTargetState.containerTitle = containerName;
                        g_dropTargetState.hoveredLeaf = nullptr;
                        g_dropTargetState.zone = DropTargetState::DropZone::Center;
                        break; // Stop checking other containers
                    }

                    DockNode* leaf = FindLeafAtPoint(containerState.root.get(), mouseX, mouseY);
                    if (leaf != nullptr && leaf->type == DockNode::Type::Leaf) {
                        DropTargetState::DropZone zone = DetermineDropZone(leaf->computedRect, mouseX, mouseY);
                        g_dropTargetState.active = true;
                        g_dropTargetState.containerTitle = containerName;
                        g_dropTargetState.hoveredLeaf = leaf;
                        g_dropTargetState.zone = zone;
                        break; // Stop checking other containers
                    }
                }
            }

            float globalX = 0.0f;
            float globalY = 0.0f;
            SDL_GetGlobalMouseState(&globalX, &globalY);
            const float dx = globalX - g_panelDrag.globalMouseStartX;
            const float dy = globalY - g_panelDrag.globalMouseStartY;

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

            clamp_bounds(nextBounds, g_panelDrag.mode != PanelDragMode::Move);
            panelBounds = nextBounds;
            write_state_from_bounds(panelState, panelBounds);
        }
    }

    if (!isPanelDocked) {
        clamp_bounds(panelBounds);
        write_state_from_bounds(panelState, panelBounds);
    }

    Spherical::FontStyle targetFontStyle = Spherical::FontStyle::Title;
    if (!m_panelTitleFontStack.empty()) {
        targetFontStyle = m_panelTitleFontStack.back();
    }
    const nk_user_font* titleFont = Spherical::FontRenderer::GetFontHandle(targetFontStyle);
    if (titleFont == nullptr) {
        titleFont = (m_ctx != nullptr) ? m_ctx->style.font : nullptr;
    }
    const bool pushedTitleFont = (titleFont != nullptr);
    if (pushedTitleFont) {
        nk_style_push_font(m_ctx, titleFont);
    }

    bool pushedTitlePadding = false;
    if (!m_panelTitlePaddingStack.empty()) {
        nk_style_push_vec2(m_ctx, &m_ctx->style.window.header.padding, m_panelTitlePaddingStack.back());
        pushedTitlePadding = true;
    }

    nk_flags panelFlags = (isPanelDocked ? 0 : NK_WINDOW_BORDER);
    if (!(flags & Spherical::PanelFlags::NoTitle)) {
        panelFlags |= NK_WINDOW_TITLE;
    }
    if (flags & Spherical::PanelFlags::NoScrollbar) {
        panelFlags |= NK_WINDOW_NO_SCROLLBAR;
    }

    if (flags & Spherical::PanelFlags::NoPadding) {
        nk_style_push_vec2(m_ctx, &m_ctx->style.window.padding, nk_vec2(0.0f, 0.0f));
        m_activePanelNoPadding = true;
    } else {
        m_activePanelNoPadding = false;
    }



    const bool result = nk_begin(
        m_ctx,
        title,
        panelBounds,
        panelFlags) != 0;
    if (pushedTitleFont) {
        nk_style_pop_font(m_ctx);
    }
    if (pushedTitlePadding) {
        nk_style_pop_vec2(m_ctx);
    }

    if (result) {
        const struct nk_rect windowBounds = nk_window_get_bounds(m_ctx);
        const struct nk_rect contentRegion = nk_window_get_content_region(m_ctx);
        panelBounds = windowBounds;
        if (!isPanelDocked) {
            write_state_from_bounds(panelState, panelBounds);
        }
        m_currentPanelBounds = {windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h};
        m_currentPanelContentBounds = {contentRegion.x, contentRegion.y, contentRegion.w, contentRegion.h};

        float headerH = contentRegion.y - windowBounds.y;
        if (headerH <= 0.0f) {
            headerH = GetWindowHeaderHeight(m_ctx, titleFont);
        } else {
            headerH += 1.0f;
        }
        const struct nk_rect headerRect = nk_rect(windowBounds.x, windowBounds.y, windowBounds.w, headerH);
        const struct nk_rect topLeft = nk_rect(windowBounds.x, windowBounds.y, kCornerHandleSize, kCornerHandleSize);

        // Render undock button if panel is docked
        const float undockButtonDiameter = 16.0f;
        struct nk_rect undockButtonRect = nk_rect(
            windowBounds.x + windowBounds.w - undockButtonDiameter - 8.0f,
            windowBounds.y + std::max(2.0f, (headerH - undockButtonDiameter) * 0.5f),
            undockButtonDiameter,
            undockButtonDiameter
        );

        if (isPanelDocked && !disallowUndock && m_ctx->current != nullptr) {
            nk_command_buffer* buffer = &m_ctx->current->buffer;
            const bool undockButtonHovered = IsMouseInsideRect(m_ctx, undockButtonRect);
            const nk_color undockButtonColor = undockButtonHovered ? nk_rgb(245, 88, 88) : nk_rgb(230, 60, 60);
            const nk_color undockButtonBorder = nk_rgb(255, 230, 230);
            const float crossInset = 4.5f;

            nk_push_scissor(buffer, windowBounds);
            nk_fill_circle(buffer, undockButtonRect, undockButtonColor);
            nk_stroke_circle(buffer, undockButtonRect, 1.5f, undockButtonBorder);
            nk_stroke_line(
                buffer,
                undockButtonRect.x + crossInset,
                undockButtonRect.y + crossInset,
                undockButtonRect.x + undockButtonRect.w - crossInset,
                undockButtonRect.y + undockButtonRect.h - crossInset,
                1.5f,
                nk_rgb(255, 255, 255)
            );
            nk_stroke_line(
                buffer,
                undockButtonRect.x + undockButtonRect.w - crossInset,
                undockButtonRect.y + crossInset,
                undockButtonRect.x + crossInset,
                undockButtonRect.y + undockButtonRect.h - crossInset,
                1.5f,
                nk_rgb(255, 255, 255)
            );
            nk_push_scissor(buffer, contentRegion);

            if (WasLeftMousePressed(m_ctx) && IsMouseInsideRect(m_ctx, undockButtonRect)) {
                undock_panel_from_workspace(title);
                m_ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
            }
        }

        const struct nk_rect topRight = nk_rect(windowBounds.x + windowBounds.w - kCornerHandleSize, windowBounds.y, kCornerHandleSize, kCornerHandleSize);
        const struct nk_rect bottomLeft = nk_rect(windowBounds.x, windowBounds.y + windowBounds.h - kCornerHandleSize, kCornerHandleSize, kCornerHandleSize);
        const struct nk_rect bottomRight = nk_rect(windowBounds.x + windowBounds.w - kCornerHandleSize, windowBounds.y + windowBounds.h - kCornerHandleSize, kCornerHandleSize, kCornerHandleSize);
        const struct nk_rect edgeTop = nk_rect(windowBounds.x + kCornerHandleSize, windowBounds.y, std::max(0.0f, windowBounds.w - 2.0f * kCornerHandleSize), kEdgeHandleThickness);
        const struct nk_rect edgeRight = nk_rect(windowBounds.x + windowBounds.w - kEdgeHandleThickness, windowBounds.y + kCornerHandleSize, kEdgeHandleThickness, std::max(0.0f, windowBounds.h - 2.0f * kCornerHandleSize));
        const struct nk_rect edgeBottom = nk_rect(windowBounds.x + kCornerHandleSize, windowBounds.y + windowBounds.h - kEdgeHandleThickness, std::max(0.0f, windowBounds.w - 2.0f * kCornerHandleSize), kEdgeHandleThickness);
        const struct nk_rect edgeLeft = nk_rect(windowBounds.x, windowBounds.y + kCornerHandleSize, kEdgeHandleThickness, std::max(0.0f, windowBounds.h - 2.0f * kCornerHandleSize));

        if ((!isPanelDocked && !isLocked) && g_panelDrag.active && g_panelDrag.windowTitle == title) {
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
        } else if (!isPanelDocked && !isLocked) {
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

        // Don't allow independent move/resize drags for docked panels
        if (!g_panelDrag.active && (!isPanelDocked && !isLocked) && WasLeftMousePressed(m_ctx)) {
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
                SDL_CaptureMouse(true);
                m_ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
            }
        }

        if (!isPanelDocked && m_ctx->current != nullptr) {
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
        if (g_panelDrag.active && g_panelDrag.windowTitle == title && !IsLeftMouseDownAnywhere(m_ctx)) {
            g_panelDrag = {};
            SDL_CaptureMouse(false);
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

        if (m_ctx != nullptr && m_ctx->current != nullptr) {
            DockNode* dockedContainerRoot = nullptr;
            std::string dockedContainerTitle;
            for (auto& [containerTitle, containerState] : g_workspaceContainerStates) {
                if (containerState.root == nullptr) {
                    continue;
                }

                if (FindDockNodeByPanelTitle(containerState.root.get(), m_activePanelTitle) != nullptr) {
                    dockedContainerRoot = containerState.root.get();
                    dockedContainerTitle = containerTitle;
                    break;
                }
            }

            if (dockedContainerRoot != nullptr) {
                const DockSplitterHit hoveredHit = FindDockSplitterAtPoint(
                    dockedContainerRoot,
                    m_ctx->input.mouse.pos.x,
                    m_ctx->input.mouse.pos.y,
                    2.0f,
                    10.0f
                );

                DrawDockSplitLinesForClipRect(
                    &m_ctx->current->buffer,
                    dockedContainerRoot,
                    nk_rect(
                        m_currentPanelBounds.x,
                        m_currentPanelBounds.y,
                        m_currentPanelBounds.w,
                        m_currentPanelBounds.h
                    ),
                    (g_splitterDrag.containerTitle == dockedContainerTitle)
                        ? DockSplitterHit{g_splitterDrag.splitNode, g_splitterDrag.boundaryIndex, {}, {}}
                        : DockSplitterHit{},
                    hoveredHit.node
                        ? hoveredHit
                        : DockSplitterHit{}
                );

                // Draw manual razor-sharp 1px inner border
                const struct nk_rect oldClip = m_ctx->current->buffer.clip;
                nk_push_scissor(&m_ctx->current->buffer, nk_rect(0, 0, 9999, 9999));

                struct nk_rect borderRect;
                borderRect.x = m_currentPanelBounds.x + 0.5f;
                borderRect.y = m_currentPanelBounds.y + 0.5f;
                borderRect.w = m_currentPanelBounds.w - 1.0f;
                borderRect.h = m_currentPanelBounds.h - 1.0f;
                nk_stroke_rect(&m_ctx->current->buffer, borderRect, 0.0f, 1.0f, nk_rgb(144, 159, 174)); // docked panel 1 pixel border color (out of 255 rgb)

                nk_push_scissor(&m_ctx->current->buffer, oldClip);
            }
        }
    }
    nk_end(m_ctx);



    if (m_activePanelNoPadding) {
        nk_style_pop_vec2(m_ctx);
        m_activePanelNoPadding = false;
    }

    m_panelSubsectionStack.clear();
    m_activePanelTitle = nullptr;
}