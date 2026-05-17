#include "docking/DockOverlayOps.h"

#include "docking/DockSplitterOps.h"
#include "docking/DockTreePrimitives.h"

#include "nuklear_config.h"
#include <nuklear.h>

#include <algorithm>
#include <chrono>
#include <functional>

namespace Spherical::DockOverlayOps {

    using namespace Internal;

    float GetWorkspaceSplitterFlashAlpha(const Internal::WorkspaceContainerState& containerState,
                                         const Internal::DockNode* root,
                                         const Internal::DockNode* splitNode,
                                         std::size_t boundaryIndex) {
        if (!containerState.splitterFlash.active || splitNode == nullptr || root == nullptr) {
            return 0.0f;
        }
        if (containerState.splitterFlash.splitNode != splitNode || containerState.splitterFlash.boundaryIndex != boundaryIndex) {
            return 0.0f;
        }
        if (!Spherical::DockSplitterOps::DockTreeContainsNode(root, splitNode)) {
            return 0.0f;
        }

        constexpr float kFlashDurationSeconds = 1.0f;
        const auto now = std::chrono::steady_clock::now();
        const float elapsedSeconds = std::chrono::duration<float>(now - containerState.splitterFlash.startedAt).count();
        if (elapsedSeconds >= kFlashDurationSeconds) {
            return 0.0f;
        }
        return std::clamp(1.0f - elapsedSeconds / kFlashDurationSeconds, 0.0f, 1.0f);
    }

    void DrawWorkspaceSplitterOverlay(
        nk_context* context,
        const VkExtent2D& framebufferExtent,
        const std::unordered_map<std::string, Internal::WorkspaceContainerState>& workspaceContainerStates,
        const Internal::SplitterDragState& splitterDragState) {

        if (context == nullptr || workspaceContainerStates.empty()) {
            return;
        }

        const float width = static_cast<float>(framebufferExtent.width);
        const float height = static_cast<float>(framebufferExtent.height);
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }

        bool hasSplits = false;
        for (const auto& [containerName, containerState] : workspaceContainerStates) {
            if (containerState.root != nullptr && !DockTreePrimitives::IsDockTerminalNode(containerState.root.get())) {
                hasSplits = true;
                break;
            }
        }
        if (!hasSplits) {
            return;
        }

        const nk_color previousBackground = context->style.window.background;
        const nk_style_item previousFixedBackground = context->style.window.fixed_background;
        const nk_color transparent = nk_rgba(0, 0, 0, 0);
        context->style.window.background = transparent;
        context->style.window.fixed_background = nk_style_item_color(transparent);

        const nk_flags flags = NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT;
        if (nk_begin(context, "__SPHERICAL_WORKSPACE_SPLITTER_OVERLAY", nk_rect(0.0f, 0.0f, width, height), flags)) {
            if (context->current != nullptr) {
                nk_command_buffer* buffer = &context->current->buffer;
                nk_push_scissor(buffer, nk_rect(0.0f, 0.0f, width, height));

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

                for (const auto& [containerName, containerState] : workspaceContainerStates) {
                    if (containerState.root == nullptr || DockTreePrimitives::IsDockTerminalNode(containerState.root.get())) {
                        continue;
                    }
                    const DockSplitterHit hoveredSplitter = DockSplitterOps::FindDockSplitterAtPoint(
                        containerState.root.get(),
                        context->input.mouse.pos.x,
                        context->input.mouse.pos.y,
                        2.0f,
                        10.0f
                    );

                    std::function<void(DockNode*)> drawSplits = [&](DockNode* node) {
                        if (node == nullptr || DockTreePrimitives::IsDockTerminalNode(node)) {
                            return;
                        }

                        for (const auto& childNode : node->children) {
                            drawSplits(childNode.get());
                        }

                        for (std::size_t boundaryIndex = 0; boundaryIndex + 1 < node->children.size(); ++boundaryIndex) {
                            const bool active = splitterDragState.active &&
                                splitterDragState.containerTitle == containerName &&
                                splitterDragState.splitNode == node &&
                                splitterDragState.boundaryIndex == boundaryIndex;
                            const bool hovered = !active &&
                                hoveredSplitter.node == node &&
                                hoveredSplitter.boundaryIndex == boundaryIndex;
                            const nk_color lineColor = active
                                ? nk_rgb(255, 156, 92)
                                : (hovered ? nk_rgb(246, 216, 92) : nk_rgb(126, 132, 148));
                            const float thickness = active ? 3.0f : (hovered ? 2.5f : 2.0f);
                            const float flashAlpha = GetWorkspaceSplitterFlashAlpha(containerState, containerState.root.get(), node, boundaryIndex);
                            const nk_color finalColor = (flashAlpha > 0.0f)
                                ? blendColor(lineColor, nk_rgb(86, 225, 112), flashAlpha)
                                : lineColor;

                            nk_fill_rect(buffer, DockSplitterOps::GetDockSplitterLineRect(node, boundaryIndex, thickness), 0.0f, finalColor);
                        }
                    };

                    drawSplits(containerState.root.get());
                }
            }
        }
        nk_end(context);
        context->style.window.background = previousBackground;
        context->style.window.fixed_background = previousFixedBackground;
    }

} // namespace Spherical::DockOverlayOps



