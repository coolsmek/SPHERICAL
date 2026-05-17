// Workspace Container / BSP Docking Implementation


bool UIPainterImpl::begin_workspace_container(const char* title, int x, int y, int width, int height) {
    if (m_ctx == nullptr || title == nullptr) {
        return false;
    }

    constexpr float kMinWidth = 240.0f;
    constexpr float kMinHeight = 180.0f;
    constexpr float kDockedLeafMinWidth = 10.0f;
    constexpr float kDockedLeafMinHeight = 10.0f;
    constexpr float kCornerHandleSize = 2.0f;
    constexpr float kEdgeHandleThickness = 2.0f;
    constexpr float kDockSplitterUnifySnapThreshold = 8.0f;

    const float centerX = static_cast<float>(m_framebufferExtent.width) * 0.5f;
    const float centerY = static_cast<float>(m_framebufferExtent.height) * 0.5f;

    auto clamp_bounds = [&](struct nk_rect& bounds) {
        bounds.w = std::max(bounds.w, kMinWidth);
        bounds.h = std::max(bounds.h, kMinHeight);
    };

    auto bounds_from_state = [&](const WorkspaceContainerState& state) {
        struct nk_rect bounds = nk_rect(
            centerX + state.offsetFromCenterX,
            centerY + state.offsetFromCenterY,
            state.width,
            state.height
        );
        clamp_bounds(bounds);
        return bounds;
    };

    auto write_state_from_bounds = [&](WorkspaceContainerState& state, const struct nk_rect& bounds) {
        state.offsetFromCenterX = bounds.x - centerX;
        state.offsetFromCenterY = bounds.y - centerY;
        state.width = bounds.w;
        state.height = bounds.h;
    };

    auto write_panel_state_from_bounds = [&](const std::string& panelTitle, struct nk_rect bounds) {
        bounds.w = std::max(bounds.w, kMinWidth);
        bounds.h = std::max(bounds.h, kMinHeight);
        PanelPersistentState& panelState = g_panelStates[panelTitle];
        panelState.initialized = true;
        panelState.offsetFromCenterX = bounds.x - centerX;
        panelState.offsetFromCenterY = bounds.y - centerY;
        panelState.width = bounds.w;
        panelState.height = bounds.h;
    };

    WorkspaceContainerState& containerState = g_workspaceContainerStates[title];
    if (!containerState.initialized) {
        containerState.width = std::max(kMinWidth, static_cast<float>(width));
        containerState.height = std::max(kMinHeight, static_cast<float>(height));
        containerState.offsetFromCenterX = static_cast<float>(x) - centerX;
        containerState.offsetFromCenterY = static_cast<float>(y) - centerY;
        containerState.initialized = true;
    }

    const float predictedHeaderHeight = GetWindowHeaderHeight(m_ctx);
    float minContainerWidth = kMinWidth;
    float minContainerHeight = kMinHeight;
    if (containerState.root != nullptr) {
        const DockMinSize minContentSize = ComputeDockNodeMinimumSize(containerState.root.get(), kDockedLeafMinWidth, kDockedLeafMinHeight);
        minContainerWidth = std::max(minContainerWidth, minContentSize.width);
        minContainerHeight = std::max(minContainerHeight, minContentSize.height + predictedHeaderHeight);
    }

    struct nk_rect containerBounds = bounds_from_state(containerState);
    if (containerBounds.w < minContainerWidth) {
        containerBounds.w = minContainerWidth;
    }
    if (containerBounds.h < minContainerHeight) {
        containerBounds.h = minContainerHeight;
    }
    write_state_from_bounds(containerState, containerBounds);

    const float undockAllButtonWidth = 92.0f;
    const float undockAllButtonHeight = std::max(16.0f, predictedHeaderHeight - 8.0f);
    auto undock_all_panels_from_container = [&](WorkspaceContainerState& state, const struct nk_rect& workspaceBounds) {
        {
            std::ostringstream oss;
            oss << "[SPHERICAL][UndockAll] execute_begin title='" << title << "'";
            AppendRuntimeDiag(oss.str());
        }

        if (state.root == nullptr) {
            state.pendingUndockAll = false;
            AppendRuntimeDiag("[SPHERICAL][UndockAll] execute_noop_root_null");
            return;
        }

        auto sanitize_value = [](float value, float fallback) {
            return std::isfinite(value) ? value : fallback;
        };

        auto sanitize_extent = [&](float value, float minValue, float fallback) {
            const float safe = sanitize_value(value, fallback);
            return std::max(minValue, safe);
        };

        const float safeWorkspaceX = sanitize_value(workspaceBounds.x, 0.0f);
        const float safeWorkspaceY = sanitize_value(workspaceBounds.y, 0.0f);
        const float safeWorkspaceW = sanitize_extent(workspaceBounds.w, kMinWidth, kMinWidth);
        const float safeWorkspaceH = sanitize_extent(workspaceBounds.h, kMinHeight, kMinHeight);
        const float safeHeaderHeight = sanitize_extent(predictedHeaderHeight, 0.0f, 24.0f);
        const float framebufferW = std::max(1.0f, static_cast<float>(m_framebufferExtent.width));
        const float framebufferH = std::max(1.0f, static_cast<float>(m_framebufferExtent.height));

        std::vector<std::pair<std::string, struct nk_rect>> dockedPanels;
        std::function<void(const DockNode*)> collectDockedPanels = [&](const DockNode* node) {
            if (node == nullptr) {
                return;
            }
            if (node->type == DockNode::Type::Leaf) {
                dockedPanels.emplace_back(node->panelTitle, node->computedRect);
                return;
            }
            for (const auto& child : node->children) {
                collectDockedPanels(child.get());
            }
        };
        collectDockedPanels(state.root.get());

        int panelIndex = 0;
        for (const auto& [panelTitle, dockedRect] : dockedPanels) {
            if (panelTitle.empty()) {
                continue;
            }

            // Prefer each panel's original initialized size to avoid giant overlapping windows
            // when undocking from a maximized workspace layout.
            const auto panelStateIt = g_panelStates.find(panelTitle);
            const float preferredW = (panelStateIt != g_panelStates.end() && panelStateIt->second.initialized)
                ? panelStateIt->second.initialWidth
                : kMinWidth;
            const float preferredH = (panelStateIt != g_panelStates.end() && panelStateIt->second.initialized)
                ? panelStateIt->second.initialHeight
                : kMinHeight;

            const float restoredW = sanitize_extent(preferredW, kMinWidth, std::max(kMinWidth, dockedRect.w));
            const float restoredH = sanitize_extent(preferredH, kMinHeight, std::max(kMinHeight, dockedRect.h));
            const float defaultX = safeWorkspaceX + 24.0f + static_cast<float>(panelIndex) * 28.0f;
            const float defaultY = safeWorkspaceY + safeHeaderHeight + 24.0f + static_cast<float>(panelIndex) * 28.0f;
            const float restoredX = sanitize_value(defaultX, 20.0f);
            const float restoredY = sanitize_value(defaultY, 20.0f);

            struct nk_rect restoredBounds = nk_rect(
                std::clamp(restoredX, 0.0f, std::max(0.0f, framebufferW - restoredW)),
                std::clamp(restoredY, 0.0f, std::max(0.0f, framebufferH - restoredH)),
                restoredW,
                restoredH
            );

            // Keep restored panels roughly anchored around the workspace area while staying on-screen.
            restoredBounds.x = std::clamp(restoredBounds.x, safeWorkspaceX - safeWorkspaceW, safeWorkspaceX + safeWorkspaceW);
            restoredBounds.y = std::clamp(restoredBounds.y, safeWorkspaceY - safeWorkspaceH, safeWorkspaceY + safeWorkspaceH);

            write_panel_state_from_bounds(panelTitle, restoredBounds);
            ++panelIndex;
        }

        {
            std::ostringstream oss;
            oss << "[SPHERICAL][UndockAll] restored_panels=" << panelIndex;
            AppendRuntimeDiag(oss.str());
        }

        state.root.reset();
        state.splitterLinkGroups.clear();
        state.splitterIntersections.clear();
        state.pendingUndockAll = false;
        state.pendingSplitterReconcile = false;
        // After bulk undock, return the container to its authored default bounds.
        // This prevents a maximized/oversized workspace window from dominating the frame
        // after all panels become free-floating again.
        state.width = std::max(kMinWidth, static_cast<float>(width));
        state.height = std::max(kMinHeight, static_cast<float>(height));
        state.offsetFromCenterX = static_cast<float>(x) - centerX;
        state.offsetFromCenterY = static_cast<float>(y) - centerY;
        // Clear all transient UI interaction state so no stale drag/capture persists after bulk undock.
        g_panelDrag = {};
        SDL_CaptureMouse(false);
        g_scrollbarDrag = {};
        g_splitterDrag = {};
        g_dropTargetState = {};

        AppendRuntimeDiag("[SPHERICAL][UndockAll] execute_end");
    };

    const struct nk_rect undockAllButtonRect = nk_rect(
        containerBounds.x + containerBounds.w - undockAllButtonWidth - 8.0f,
        containerBounds.y + std::max(2.0f, (predictedHeaderHeight - undockAllButtonHeight) * 0.5f),
        undockAllButtonWidth,
        undockAllButtonHeight
    );

    const struct nk_rect titleBarRect = nk_rect(containerBounds.x, containerBounds.y, containerBounds.w, predictedHeaderHeight);
    const struct nk_rect topLeft = nk_rect(containerBounds.x, containerBounds.y, kCornerHandleSize, kCornerHandleSize);
    const struct nk_rect topRight = nk_rect(containerBounds.x + containerBounds.w - kCornerHandleSize, containerBounds.y, kCornerHandleSize, kCornerHandleSize);
    const struct nk_rect bottomLeft = nk_rect(containerBounds.x, containerBounds.y + containerBounds.h - kCornerHandleSize, kCornerHandleSize, kCornerHandleSize);
    const struct nk_rect bottomRight = nk_rect(containerBounds.x + containerBounds.w - kCornerHandleSize, containerBounds.y + containerBounds.h - kCornerHandleSize, kCornerHandleSize, kCornerHandleSize);
    const struct nk_rect edgeTop = nk_rect(containerBounds.x + kCornerHandleSize, containerBounds.y, std::max(0.0f, containerBounds.w - 2.0f * kCornerHandleSize), kEdgeHandleThickness);
    const struct nk_rect edgeRight = nk_rect(containerBounds.x + containerBounds.w - kEdgeHandleThickness, containerBounds.y + kCornerHandleSize, kEdgeHandleThickness, std::max(0.0f, containerBounds.h - 2.0f * kCornerHandleSize));
    const struct nk_rect edgeBottom = nk_rect(containerBounds.x + kCornerHandleSize, containerBounds.y + containerBounds.h - kEdgeHandleThickness, std::max(0.0f, containerBounds.w - 2.0f * kCornerHandleSize), kEdgeHandleThickness);
    const struct nk_rect edgeLeft = nk_rect(containerBounds.x, containerBounds.y + kCornerHandleSize, kEdgeHandleThickness, std::max(0.0f, containerBounds.h - 2.0f * kCornerHandleSize));
    const struct nk_rect predictedBodyRect = nk_rect(
        containerBounds.x,
        containerBounds.y + predictedHeaderHeight,
        containerBounds.w,
        std::max(0.0f, containerBounds.h - predictedHeaderHeight)
    );

    DockSplitterHit preLayoutSplitterHit{};
    if (containerState.root != nullptr && predictedBodyRect.w > 0.0f && predictedBodyRect.h > 0.0f) {
        NormalizeDockNodeLayoutForBounds(containerState.root.get(), predictedBodyRect, kDockedLeafMinWidth, kDockedLeafMinHeight);
        preLayoutSplitterHit = FindDockSplitterAtPoint(
            containerState.root.get(),
            m_ctx->input.mouse.pos.x,
            m_ctx->input.mouse.pos.y,
            kDockSplitterVisualThickness,
            kDockSplitterHitThickness
        );
    }

    // Handle workspace container drag
    if (g_panelDrag.active && g_panelDrag.windowTitle == title) {
        if (!IsLeftMouseDownAnywhere(m_ctx)) {
            g_panelDrag = {};
            SDL_CaptureMouse(false);
        } else {
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

            if (nextBounds.w < minContainerWidth) {
                if (g_panelDrag.mode == PanelDragMode::ResizeTopLeft || g_panelDrag.mode == PanelDragMode::ResizeBottomLeft ||
                    g_panelDrag.mode == PanelDragMode::ResizeLeft) {
                    nextBounds.x = g_panelDrag.panelStartBounds.x + (g_panelDrag.panelStartBounds.w - minContainerWidth);
                }
                nextBounds.w = minContainerWidth;
            }

            if (nextBounds.h < minContainerHeight) {
                if (g_panelDrag.mode == PanelDragMode::ResizeTopLeft || g_panelDrag.mode == PanelDragMode::ResizeTopRight ||
                    g_panelDrag.mode == PanelDragMode::ResizeTop) {
                    nextBounds.y = g_panelDrag.panelStartBounds.y + (g_panelDrag.panelStartBounds.h - minContainerHeight);
                }
                nextBounds.h = minContainerHeight;
            }

            // Defensive clamping: prevent the workspace from exceeding framebuffer bounds
            const float maxContainerWidth = static_cast<float>(m_framebufferExtent.width);
            const float maxContainerHeight = static_cast<float>(m_framebufferExtent.height);

            if (nextBounds.w > maxContainerWidth) {
                nextBounds.w = maxContainerWidth;
            }

            if (nextBounds.h > maxContainerHeight) {
                nextBounds.h = maxContainerHeight;
            }

            clamp_bounds(nextBounds);
            containerBounds = nextBounds;
            write_state_from_bounds(containerState, containerBounds);
            
            // Diagnostic logging for large resize attempts
            if (nextBounds.w > 2000.0f || nextBounds.h > 1000.0f) {
                std::ostringstream oss;
                oss << "[WORKSPACE_RESIZE_LARGE] mode=" << static_cast<int>(g_panelDrag.mode)
                    << " W=" << nextBounds.w << " H=" << nextBounds.h 
                    << " fbW=" << maxContainerWidth << " fbH=" << maxContainerHeight;
                AppendRuntimeDiag(oss.str());
            }
        }
    }

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
            case PanelDragMode::Move:
            case PanelDragMode::None:
            default:
                break;
        }
    } else if (preLayoutSplitterHit.node == nullptr) {
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

    if (!g_panelDrag.active && !g_splitterDrag.active && preLayoutSplitterHit.node == nullptr && WasLeftMousePressed(m_ctx)) {
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
        } else if (IsMouseInsideRect(m_ctx, titleBarRect) && !IsMouseInsideRect(m_ctx, undockAllButtonRect)) {
            startMode = PanelDragMode::Move;
        }

        if (startMode != PanelDragMode::None) {
            g_panelDrag.active = true;
            g_panelDrag.windowTitle = title;
            g_panelDrag.mode = startMode;
            g_panelDrag.mouseStartX = m_ctx->input.mouse.pos.x;
            g_panelDrag.mouseStartY = m_ctx->input.mouse.pos.y;
            g_panelDrag.panelStartBounds = containerBounds;
            float globalX = 0.0f;
            float globalY = 0.0f;
            SDL_GetGlobalMouseState(&globalX, &globalY);
            g_panelDrag.globalMouseStartX = globalX;
            g_panelDrag.globalMouseStartY = globalY;
            SDL_CaptureMouse(true);
            m_ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
        }
    }

    clamp_bounds(containerBounds);
    containerBounds.w = std::max(containerBounds.w, minContainerWidth);
    containerBounds.h = std::max(containerBounds.h, minContainerHeight);
    write_state_from_bounds(containerState, containerBounds);

    // Determine if this is an active drag (move or resize) for this container.
    // During active drag we own all bounds calculations and must not let Nuklear
    // overwrite them via nk_window_get_bounds() readback.
    const bool isActiveDrag = (g_panelDrag.active && g_panelDrag.windowTitle == title);

    // Force Nuklear's internal window record to our computed bounds BEFORE calling nk_begin().
    // nk_begin() uses the window's existing stored bounds when the window already exists on
    // subsequent frames. Without this, Nuklear ignores the rect argument on existing windows
    // and returns its stale/constrained bounds from nk_window_get_bounds(), producing a
    // feedback loop that makes the window stick / jitter during large resize operations.
    if (isActiveDrag) {
        nk_window* win = nk_window_find(m_ctx, title);
        if (win != nullptr) {
            win->bounds = containerBounds;
        }
    }

    const bool result = nk_begin(
        m_ctx,
        title,
        containerBounds,
        NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT) != 0;

    // During an active drag, use our computed bounds directly.
    // Outside of a drag, read back what Nuklear calculated.
    struct nk_rect windowBounds;
    if (isActiveDrag) {
        // During drag: Nuklear has our bounds, just query them back
        windowBounds = nk_window_get_bounds(m_ctx);
    } else {
        // Outside drag: get Nuklear's calculated/adjusted bounds
        windowBounds = nk_window_get_bounds(m_ctx);
        containerBounds = windowBounds;
        write_state_from_bounds(containerState, containerBounds);
    }

    // During active drag use our freshly-computed bounds for all sub-layout work;
    // otherwise use Nuklear's readback.
    const struct nk_rect& layoutBounds = isActiveDrag ? containerBounds : windowBounds;

    struct nk_rect contentRect = nk_window_get_content_region(m_ctx);
    float actualHeaderHeight = contentRect.y - windowBounds.y;
    if (actualHeaderHeight <= 0.0f) {
        actualHeaderHeight = predictedHeaderHeight;
    } else {
        actualHeaderHeight += 1.0f;
    }
    containerState.headerHeight = actualHeaderHeight;

    const struct nk_rect actualBodyRect = nk_rect(
        layoutBounds.x,
        layoutBounds.y + actualHeaderHeight,
        layoutBounds.w,
        std::max(0.0f, layoutBounds.h - actualHeaderHeight)
    );

    if (actualBodyRect.w > 0.0f && actualBodyRect.h > 0.0f && containerState.root != nullptr) {
        NormalizeDockNodeLayoutForBounds(containerState.root.get(), actualBodyRect, kDockedLeafMinWidth, kDockedLeafMinHeight);
    }

    if (containerState.pendingUndockAll) {
        undock_all_panels_from_container(containerState, windowBounds);
    }

    if (containerState.pendingSplitterReconcile && containerState.root != nullptr &&
        actualBodyRect.w > 0.0f && actualBodyRect.h > 0.0f) {
        ReconcileWorkspaceContainerSplitters(
            containerState.root,
            containerState,
            actualBodyRect,
            kDockedLeafMinWidth,
            kDockedLeafMinHeight);
    }

    if (containerState.root != nullptr) {
        PruneWorkspaceSplitterLinks(containerState);
        PruneWorkspaceSplitterIntersections(containerState);

        auto adjust_splitter_drag_to_position = [&](DockNode* splitNode, std::size_t boundaryIndex, float boundaryPosition) {
            if (splitNode == nullptr || IsDockTerminalNode(splitNode)) {
                return;
            }

            const float clampedBoundaryPosition = ClampLinkedDockSplitterPosition(
                containerState,
                splitNode,
                boundaryIndex,
                boundaryPosition,
                kDockedLeafMinWidth,
                kDockedLeafMinHeight);
            const std::vector<DockSplitterRef> linkedRefs = GetLinkedDockSplitterRefs(containerState, splitNode, boundaryIndex);
            for (const DockSplitterRef& linkedRef : linkedRefs) {
                if (linkedRef.node == nullptr || IsDockTerminalNode(linkedRef.node)) {
                    continue;
                }

                AdjustDockSplitterToPosition(
                    linkedRef.node,
                    linkedRef.boundaryIndex,
                    clampedBoundaryPosition,
                    kDockedLeafMinWidth,
                    kDockedLeafMinHeight);
            }

            NormalizeDockNodeLayoutForBounds(containerState.root.get(), actualBodyRect, kDockedLeafMinWidth, kDockedLeafMinHeight);
        };

        if (g_splitterDrag.active && g_splitterDrag.containerTitle == title) {
            if (!DockTreeContainsNode(containerState.root.get(), g_splitterDrag.splitNode)) {
                g_splitterDrag = {};
            } else if (!IsLeftMouseDownAnywhere(m_ctx)) {
                if (g_splitterDrag.splitNode != nullptr && !IsDockTerminalNode(g_splitterDrag.splitNode)) {
                    DockNode* splitNode = g_splitterDrag.splitNode;
                    const float boundaryPosition = (splitNode->splitDirection == Spherical::DockLayout::SideBySide)
                        ? m_ctx->input.mouse.pos.x
                        : m_ctx->input.mouse.pos.y;

                    adjust_splitter_drag_to_position(splitNode, g_splitterDrag.boundaryIndex, boundaryPosition);

                    DockSplitterHit unifiedHit{};
                    if (TryUnifyDockSplitterBoundary(
                        containerState.root,
                        containerState,
                        splitNode,
                        g_splitterDrag.boundaryIndex,
                        kDockSplitterUnifySnapThreshold,
                        kDockedLeafMinWidth,
                        kDockedLeafMinHeight,
                        unifiedHit)) {
                        CascadeWorkspaceContainerSplitterUnifications(
                            containerState.root,
                            containerState,
                            actualBodyRect,
                            kDockSplitterUnifySnapThreshold,
                            kDockedLeafMinWidth,
                            kDockedLeafMinHeight);
                    }
                }

                g_splitterDrag = {};
            } else if (g_splitterDrag.splitNode != nullptr && !IsDockTerminalNode(g_splitterDrag.splitNode)) {
                DockNode* splitNode = g_splitterDrag.splitNode;
                const float boundaryPosition = (splitNode->splitDirection == Spherical::DockLayout::SideBySide)
                    ? m_ctx->input.mouse.pos.x
                    : m_ctx->input.mouse.pos.y;

                adjust_splitter_drag_to_position(splitNode, g_splitterDrag.boundaryIndex, boundaryPosition);

                if (splitNode != nullptr) {
                    RequestCursor(splitNode->splitDirection == Spherical::DockLayout::SideBySide
                        ? CursorRequest::ResizeEw
                        : CursorRequest::ResizeNs);
                }
            }
        }

        if (!g_panelDrag.active && !g_splitterDrag.active) {
            const DockSplitterHit hoveredSplitter = FindDockSplitterAtPoint(
                containerState.root.get(),
                m_ctx->input.mouse.pos.x,
                m_ctx->input.mouse.pos.y,
                kDockSplitterVisualThickness,
                kDockSplitterHitThickness
            );

            if (hoveredSplitter.node != nullptr) {
                RequestCursor(hoveredSplitter.node->splitDirection == Spherical::DockLayout::SideBySide
                    ? CursorRequest::ResizeEw
                    : CursorRequest::ResizeNs);

                if (WasLeftMousePressed(m_ctx)) {
                    g_splitterDrag.active = true;
                    g_splitterDrag.containerTitle = title;
                    g_splitterDrag.splitNode = hoveredSplitter.node;
                    g_splitterDrag.boundaryIndex = hoveredSplitter.boundaryIndex;
                    m_ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
                }
            }
        }
    }

    if (result && m_ctx->current != nullptr) {
        nk_command_buffer* buffer = &m_ctx->current->buffer;
        const auto blendColor = [](const nk_color& base, const nk_color& accent, float t) {
            const float clampedT = std::clamp(t, 0.0f, 1.0f);
            const auto blendChannel = [clampedT](nk_byte a, nk_byte b) -> nk_byte {
                const float blended = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * clampedT;
                return static_cast<nk_byte>(std::clamp(blended, 0.0f, 255.0f) + 0.5f);
            };
            return nk_rgba(
                blendChannel(base.r, accent.r),
                blendChannel(base.g, accent.g),
                blendChannel(base.b, accent.b),
                blendChannel(base.a, accent.a));
        };
        const struct nk_rect actualHeaderRect = nk_rect(windowBounds.x, windowBounds.y, windowBounds.w, actualHeaderHeight);
        const struct nk_rect actualUndockAllButtonRect = nk_rect(
            windowBounds.x + windowBounds.w - undockAllButtonWidth - 8.0f,
            windowBounds.y + std::max(2.0f, (actualHeaderHeight - undockAllButtonHeight) * 0.5f),
            undockAllButtonWidth,
            undockAllButtonHeight
        );

        auto draw_dashed_line = [&](float x0, float y0, float x1, float y1, const nk_color& color, float thickness) {
            const float dx = x1 - x0;
            const float dy = y1 - y0;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length <= 0.0f) {
                return;
            }

            const float dashLength = 8.0f;
            const float gapLength = 5.0f;
            const float stepX = dx / length;
            const float stepY = dy / length;
            for (float offset = 0.0f; offset < length; offset += dashLength + gapLength) {
                const float endOffset = std::min(length, offset + dashLength);
                nk_stroke_line(
                    buffer,
                    x0 + stepX * offset,
                    y0 + stepY * offset,
                    x0 + stepX * endOffset,
                    y0 + stepY * endOffset,
                    thickness,
                    color
                );
            }
        };

        auto draw_dashed_rect = [&](const struct nk_rect& rect, const nk_color& color, float thickness) {
            draw_dashed_line(rect.x, rect.y, rect.x + rect.w, rect.y, color, thickness);
            draw_dashed_line(rect.x + rect.w, rect.y, rect.x + rect.w, rect.y + rect.h, color, thickness);
            draw_dashed_line(rect.x + rect.w, rect.y + rect.h, rect.x, rect.y + rect.h, color, thickness);
            draw_dashed_line(rect.x, rect.y + rect.h, rect.x, rect.y, color, thickness);
        };

        const bool hasDockedPanels = (CountDockNodeLeaves(containerState.root.get()) > 0);
        const bool undockAllHovered = IsMouseInsideRect(m_ctx, actualUndockAllButtonRect);
        const nk_color undockAllFill = !hasDockedPanels ? nk_rgb(70, 74, 84)
            : (undockAllHovered ? nk_rgb(185, 88, 72) : nk_rgb(145, 70, 58));
        const nk_color undockAllBorder = !hasDockedPanels ? nk_rgb(95, 100, 114)
            : nk_rgb(225, 180, 170);

        nk_push_scissor(buffer, windowBounds);
        nk_fill_rect(buffer, actualUndockAllButtonRect, 4.0f, undockAllFill);
        nk_stroke_rect(buffer, actualUndockAllButtonRect, 4.0f, 1.0f, undockAllBorder);

        if (m_ctx->style.font != nullptr) {
            static const char* undockAllLabel = "Undock All";
            nk_draw_text(
                buffer,
                actualUndockAllButtonRect,
                undockAllLabel,
                static_cast<int>(std::strlen(undockAllLabel)),
                m_ctx->style.font,
                nk_rgba(0, 0, 0, 0),
                !hasDockedPanels ? nk_rgb(170, 176, 188) : nk_rgb(245, 245, 245)
            );
        }

        if (hasDockedPanels && WasLeftMousePressed(m_ctx) && IsMouseInsideRect(m_ctx, actualUndockAllButtonRect)) {
            undock_all_panels_from_container(containerState, windowBounds);
            m_ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
        }

        draw_dashed_rect(actualHeaderRect, nk_rgb(130, 150, 176), 1.0f);
        draw_dashed_rect(windowBounds, nk_rgb(145, 170, 205), 1.25f);
        nk_push_scissor(buffer, actualBodyRect);

        if (containerState.root == nullptr && actualBodyRect.w > 40.0f && actualBodyRect.h > 24.0f) {
            const nk_user_font* font = m_ctx->style.font;
            if (font != nullptr) {
                static const char* emptyLabel = "Drop panels here";
                nk_draw_text(
                    buffer,
                    nk_rect(actualBodyRect.x + 12.0f,
                            actualBodyRect.y + std::max(8.0f, actualBodyRect.h * 0.5f - 10.0f),
                            std::max(0.0f, actualBodyRect.w - 24.0f),
                            20.0f),
                    emptyLabel,
                    static_cast<int>(std::strlen(emptyLabel)),
                    font,
                    nk_rgba(0, 0, 0, 0),
                    nk_rgb(190, 210, 235));
            }

            draw_dashed_rect(actualBodyRect, nk_rgb(95, 110, 130), 1.0f);
        }

        if (containerState.root != nullptr) {
            std::function<void(DockNode*)> drawDividers = [&](DockNode* node) {
                if (node == nullptr || IsDockTerminalNode(node)) {
                    return;
                }

                for (const auto& child : node->children) {
                    drawDividers(child.get());
                }

                for (std::size_t boundaryIndex = 0; boundaryIndex + 1 < node->children.size(); ++boundaryIndex) {
                    const bool active = g_splitterDrag.active &&
                        g_splitterDrag.containerTitle == title &&
                        g_splitterDrag.splitNode == node &&
                        g_splitterDrag.boundaryIndex == boundaryIndex;
                    const float thickness = active ? 3.0f : 2.0f;
                    const nk_color dividerColor = active ? nk_rgb(255, 156, 92) : nk_rgb(126, 132, 148);
                    const float flashAlpha = GetWorkspaceSplitterFlashAlpha(containerState, containerState.root.get(), node, boundaryIndex);
                    const nk_color finalColor = (flashAlpha > 0.0f)
                        ? blendColor(dividerColor, nk_rgb(86, 225, 112), flashAlpha)
                        : dividerColor;
                    nk_fill_rect(buffer, GetDockSplitterLineRect(node, boundaryIndex, thickness), 0.0f, finalColor);
                }
            };

            drawDividers(containerState.root.get());
        }


    }

    return result;
}

void UIPainterImpl::end_workspace_container() {
    nk_end(m_ctx);
}

int UIPainterImpl::get_workspace_panel_count(const char* containerTitle) const {
    if (containerTitle == nullptr) {
        return 0;
    }
    const auto it = g_workspaceContainerStates.find(containerTitle);
    if (it == g_workspaceContainerStates.end() || it->second.root == nullptr) {
        return 0;
    }
    return CountDockNodeLeaves(it->second.root.get());
}

Spherical::DockLayout UIPainterImpl::get_workspace_dock_layout(const char* containerTitle) const {
    if (containerTitle == nullptr) {
        return Spherical::DockLayout::SideBySide;
    }
    const auto it = g_workspaceContainerStates.find(containerTitle);
    if (it == g_workspaceContainerStates.end() || it->second.root == nullptr ||
        it->second.root->type != DockNode::Type::Split) {
        return Spherical::DockLayout::SideBySide;
    }
    return it->second.root->splitDirection;
}

void UIPainterImpl::undock_panel_from_workspace(const char* panelTitle) {
    if (panelTitle == nullptr) {
        return;
    }
    const std::string titleStr(panelTitle);

    const float centerX = static_cast<float>(m_framebufferExtent.width) * 0.5f;
    const float centerY = static_cast<float>(m_framebufferExtent.height) * 0.5f;
    for (auto& [containerName, containerState] : g_workspaceContainerStates) {
        if (containerState.root == nullptr) {
            continue;
        }
        DockNode* leafNode = FindDockNodeByPanelTitle(containerState.root.get(), titleStr);
        if (leafNode != nullptr) {
            const struct nk_rect floatingBounds = InsetDockedPanelRect(leafNode->computedRect);

            PanelPersistentState& panelState = g_panelStates[panelTitle];
            panelState.initialized = true;
            panelState.width = std::max(240.0f, floatingBounds.w);
            panelState.height = std::max(180.0f, floatingBounds.h);
            panelState.offsetFromCenterX = floatingBounds.x - centerX;
            panelState.offsetFromCenterY = floatingBounds.y - centerY;

            RemoveDockNodeAndReflow(containerState.root, &containerState, leafNode);
            return;
        }
    }
}

