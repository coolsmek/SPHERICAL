#include "docking/DockLayoutOps.h"

#include "docking/DockSplitterOps.h"
#include "docking/DockTreePrimitives.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {
    constexpr float kDockedPanelPadding = Spherical::Internal::kDockedPanelPadding;

    float GetDockMinSizeMainAxisExtent(const Spherical::DockLayoutOps::DockMinSize& minSize, Spherical::DockLayout splitDirection) {
        return (splitDirection == Spherical::DockLayout::SideBySide) ? minSize.width : minSize.height;
    }
}

namespace Spherical::DockLayoutOps {

    DockMinSize ComputeDockNodeMinimumSize(const Internal::DockNode* node, float leafMinWidth, float leafMinHeight) {
        if (node == nullptr) {
            return {};
        }

        if (DockTreePrimitives::IsDockTerminalNode(node)) {
            return {leafMinWidth + kDockedPanelPadding * 2.0f, leafMinHeight + kDockedPanelPadding * 2.0f};
        }

        DockMinSize result{};
        bool firstChild = true;
        for (const auto& child : node->children) {
            const DockMinSize childMin = ComputeDockNodeMinimumSize(child.get(), leafMinWidth, leafMinHeight);
            if (firstChild) {
                result = childMin;
                firstChild = false;
                continue;
            }

            if (node->splitDirection == DockLayout::SideBySide) {
                result.width += childMin.width;
                result.height = std::max(result.height, childMin.height);
            } else {
                result.width = std::max(result.width, childMin.width);
                result.height += childMin.height;
            }
        }

        return result;
    }

    DockMinSize RebalanceDockNodeSplitRatios(Internal::DockNode* node, float leafMinWidth, float leafMinHeight) {
        if (node == nullptr) {
            return {};
        }

        if (DockTreePrimitives::IsDockTerminalNode(node)) {
            return {leafMinWidth + kDockedPanelPadding * 2.0f, leafMinHeight + kDockedPanelPadding * 2.0f};
        }

        DockTreePrimitives::EnsureDockSplitStorage(node);
        node->preferredChildExtents.assign(node->children.size(), 0.0f);
        node->computedChildExtents.assign(node->children.size(), 0.0f);

        for (std::size_t i = 0; i < node->children.size(); ++i) {
            const DockMinSize childMin = RebalanceDockNodeSplitRatios(node->children[i].get(), leafMinWidth, leafMinHeight);
            node->preferredChildExtents[i] = std::max(1.0f, GetDockMinSizeMainAxisExtent(childMin, node->splitDirection));
        }

        if (node->children.size() == 2) {
            const float totalPreferred = DockTreePrimitives::SumDockExtents(node->preferredChildExtents);
            node->splitRatio = (totalPreferred > 0.0f)
                ? std::clamp(node->preferredChildExtents[0] / totalPreferred, 0.0f, 1.0f)
                : 0.5f;
            node->preferredSplitRatio = node->splitRatio;
        }

        return ComputeDockNodeMinimumSize(node, leafMinWidth, leafMinHeight);
    }

    bool GetDockSplitterClampRange(const Internal::DockNode* node,
                                   std::size_t boundaryIndex,
                                   float leafMinWidth,
                                   float leafMinHeight,
                                   float& outMinPosition,
                                   float& outMaxPosition) {
        if (node == nullptr || DockTreePrimitives::IsDockTerminalNode(node) || boundaryIndex + 1 >= node->children.size()) {
            outMinPosition = 0.0f;
            outMaxPosition = 0.0f;
            return false;
        }

        const Internal::DockNode* leadingChild = node->children[boundaryIndex].get();
        const Internal::DockNode* trailingChild = node->children[boundaryIndex + 1].get();
        if (leadingChild == nullptr || trailingChild == nullptr) {
            outMinPosition = 0.0f;
            outMaxPosition = 0.0f;
            return false;
        }

        const float pairStart = (node->splitDirection == DockLayout::SideBySide)
            ? leadingChild->computedRect.x
            : leadingChild->computedRect.y;
        const float pairEnd = (node->splitDirection == DockLayout::SideBySide)
            ? (trailingChild->computedRect.x + trailingChild->computedRect.w)
            : (trailingChild->computedRect.y + trailingChild->computedRect.h);
        const float pairExtent = std::max(0.0f, pairEnd - pairStart);
        if (pairExtent <= 0.0f) {
            outMinPosition = pairStart;
            outMaxPosition = pairStart;
            return true;
        }

        const float leadingMin = GetDockMinSizeMainAxisExtent(
            ComputeDockNodeMinimumSize(leadingChild, leafMinWidth, leafMinHeight),
            node->splitDirection);
        const float trailingMin = GetDockMinSizeMainAxisExtent(
            ComputeDockNodeMinimumSize(trailingChild, leafMinWidth, leafMinHeight),
            node->splitDirection);

        if (leadingMin + trailingMin < pairExtent) {
            outMinPosition = pairStart + leadingMin;
            outMaxPosition = pairEnd - trailingMin;
        } else {
            outMinPosition = pairStart;
            outMaxPosition = pairEnd;
        }
        return true;
    }

    float ClampDockSplitterPosition(const Internal::DockNode* node,
                                    std::size_t boundaryIndex,
                                    float proposedPosition,
                                    float leafMinWidth,
                                    float leafMinHeight) {
        float minPosition = 0.0f;
        float maxPosition = 0.0f;
        if (!GetDockSplitterClampRange(node, boundaryIndex, leafMinWidth, leafMinHeight, minPosition, maxPosition)) {
            return proposedPosition;
        }

        return std::clamp(proposedPosition, minPosition, maxPosition);
    }

    float ClampLinkedDockSplitterPosition(Internal::WorkspaceContainerState& containerState,
                                          Internal::DockNode* node,
                                          std::size_t boundaryIndex,
                                          float proposedPosition,
                                          float leafMinWidth,
                                          float leafMinHeight) {
        const std::vector<Internal::DockSplitterRef> linkedRefs =
            DockSplitterOps::GetLinkedDockSplitterRefs(containerState, node, boundaryIndex);
        bool hasRange = false;
        float combinedMin = 0.0f;
        float combinedMax = 0.0f;
        for (const Internal::DockSplitterRef& linkedRef : linkedRefs) {
            float localMin = 0.0f;
            float localMax = 0.0f;
            if (!GetDockSplitterClampRange(linkedRef.node, linkedRef.boundaryIndex, leafMinWidth, leafMinHeight, localMin, localMax)) {
                continue;
            }

            if (!hasRange) {
                combinedMin = localMin;
                combinedMax = localMax;
                hasRange = true;
            } else {
                combinedMin = std::max(combinedMin, localMin);
                combinedMax = std::min(combinedMax, localMax);
            }
        }

        if (!hasRange || combinedMin > combinedMax) {
            return ClampDockSplitterPosition(node, boundaryIndex, proposedPosition, leafMinWidth, leafMinHeight);
        }

        return std::clamp(proposedPosition, combinedMin, combinedMax);
    }

    void AdjustDockSplitterToPosition(Internal::DockNode* node,
                                      std::size_t boundaryIndex,
                                      float proposedPosition,
                                      float leafMinWidth,
                                      float leafMinHeight) {
        if (node == nullptr || DockTreePrimitives::IsDockTerminalNode(node) || boundaryIndex + 1 >= node->children.size()) {
            return;
        }

        DockTreePrimitives::EnsureDockSplitStorage(node);
        for (std::size_t i = 0; i < node->children.size(); ++i) {
            const float computedExtent = DockTreePrimitives::GetDockNodeMainAxisExtent(node->children[i].get(), node->splitDirection);
            if (std::isfinite(computedExtent) && computedExtent >= 0.0f) {
                node->preferredChildExtents[i] = computedExtent;
                node->computedChildExtents[i] = computedExtent;
            }
        }

        Internal::DockNode* leadingChild = node->children[boundaryIndex].get();
        Internal::DockNode* trailingChild = node->children[boundaryIndex + 1].get();
        if (leadingChild == nullptr || trailingChild == nullptr) {
            return;
        }

        const float pairStart = (node->splitDirection == DockLayout::SideBySide)
            ? leadingChild->computedRect.x
            : leadingChild->computedRect.y;
        const float pairEnd = (node->splitDirection == DockLayout::SideBySide)
            ? (trailingChild->computedRect.x + trailingChild->computedRect.w)
            : (trailingChild->computedRect.y + trailingChild->computedRect.h);
        const float clampedPosition = ClampDockSplitterPosition(node, boundaryIndex, proposedPosition, leafMinWidth, leafMinHeight);

        node->preferredChildExtents[boundaryIndex] = std::max(0.0f, clampedPosition - pairStart);
        node->preferredChildExtents[boundaryIndex + 1] = std::max(0.0f, pairEnd - clampedPosition);
        node->computedChildExtents[boundaryIndex] = node->preferredChildExtents[boundaryIndex];
        node->computedChildExtents[boundaryIndex + 1] = node->preferredChildExtents[boundaryIndex + 1];
        DockTreePrimitives::RefreshDockGroupLegacyRatios(node);
    }

    void NormalizeDockNodeLayoutForBounds(Internal::DockNode* node,
                                          const struct ::nk_rect& parentRect,
                                          float leafMinWidth,
                                          float leafMinHeight) {
        if (node == nullptr) {
            return;
        }

        node->computedRect = parentRect;
        if (DockTreePrimitives::IsDockTerminalNode(node)) {
            return;
        }

        DockTreePrimitives::FlattenSameAxisChildGroups(node);
        DockTreePrimitives::EnsureDockSplitStorage(node);
        const std::size_t childCount = node->children.size();
        if (childCount == 0) {
            return;
        }

        const bool horizontal = (node->splitDirection == DockLayout::SideBySide);
        const float availableMainExtent = horizontal ? parentRect.w : parentRect.h;
        std::vector<float> minExtents(childCount, 0.0f);
        float totalMinExtent = 0.0f;
        for (std::size_t i = 0; i < childCount; ++i) {
            const DockMinSize childMin = ComputeDockNodeMinimumSize(node->children[i].get(), leafMinWidth, leafMinHeight);
            minExtents[i] = GetDockMinSizeMainAxisExtent(childMin, node->splitDirection);
            totalMinExtent += minExtents[i];
        }

        std::vector<float> actualExtents(childCount, 0.0f);
        if (availableMainExtent <= 0.0f) {
            std::fill(actualExtents.begin(), actualExtents.end(), 0.0f);
        } else if (totalMinExtent > availableMainExtent && totalMinExtent > 0.0f) {
            const float scale = availableMainExtent / totalMinExtent;
            for (std::size_t i = 0; i < childCount; ++i) {
                actualExtents[i] = minExtents[i] * scale;
            }
        } else {
            const float remainingExtent = std::max(0.0f, availableMainExtent - totalMinExtent);
            std::vector<float> desiredExtra(childCount, 0.0f);
            float totalDesiredExtra = 0.0f;
            for (std::size_t i = 0; i < childCount; ++i) {
                const float preferredExtent = (i < node->preferredChildExtents.size()) ? node->preferredChildExtents[i] : minExtents[i];
                desiredExtra[i] = std::max(0.0f, preferredExtent - minExtents[i]);
                totalDesiredExtra += desiredExtra[i];
            }

            for (std::size_t i = 0; i < childCount; ++i) {
                actualExtents[i] = minExtents[i];
                if (remainingExtent <= 0.0f) {
                    continue;
                }

                if (totalDesiredExtra > 0.0f) {
                    actualExtents[i] += remainingExtent * (desiredExtra[i] / totalDesiredExtra);
                } else {
                    actualExtents[i] += remainingExtent / static_cast<float>(childCount);
                }
            }
        }

        if (!actualExtents.empty()) {
            float assignedExtent = 0.0f;
            for (std::size_t i = 0; i + 1 < actualExtents.size(); ++i) {
                assignedExtent += actualExtents[i];
            }
            actualExtents.back() = std::max(0.0f, availableMainExtent - assignedExtent);
        }

        node->computedChildExtents = actualExtents;
        if (childCount == 2 && availableMainExtent > 0.0f) {
            node->splitRatio = std::clamp(actualExtents[0] / availableMainExtent, 0.0f, 1.0f);
        }

        float cursor = horizontal ? parentRect.x : parentRect.y;
        const float mainEnd = cursor + availableMainExtent;
        for (std::size_t i = 0; i < childCount; ++i) {
            const float childExtent = (i + 1 == childCount)
                ? std::max(0.0f, mainEnd - cursor)
                : std::max(0.0f, actualExtents[i]);
            struct ::nk_rect childRect = parentRect;
            if (horizontal) {
                childRect.x = cursor;
                childRect.w = childExtent;
            } else {
                childRect.y = cursor;
                childRect.h = childExtent;
            }

            NormalizeDockNodeLayoutForBounds(node->children[i].get(), childRect, leafMinWidth, leafMinHeight);
            cursor += childExtent;
        }
    }

    void DrawDockSplitLinesForClipRect(nk_command_buffer* buffer,
                                       const Internal::DockNode* root,
                                       const struct ::nk_rect& clipRect,
                                       const Internal::DockSplitterHit& activeHit,
                                       const Internal::DockSplitterHit& hoveredHit) {
        if (buffer == nullptr || root == nullptr || DockTreePrimitives::IsDockTerminalNode(root)) {
            return;
        }

        std::function<void(const Internal::DockNode*)> drawSplits = [&](const Internal::DockNode* node) {
            if (node == nullptr || DockTreePrimitives::IsDockTerminalNode(node)) {
                return;
            }

            for (const auto& child : node->children) {
                drawSplits(child.get());
            }

            for (std::size_t boundaryIndex = 0; boundaryIndex + 1 < node->children.size(); ++boundaryIndex) {
                const bool active = activeHit.node == node && activeHit.boundaryIndex == boundaryIndex;
                const bool hovered = !active && hoveredHit.node == node && hoveredHit.boundaryIndex == boundaryIndex;
                const float thickness = active ? 3.0f : (hovered ? 2.5f : 2.0f);
                const nk_color lineColor = active
                    ? nk_rgb(255, 156, 92)
                    : (hovered ? nk_rgb(246, 216, 92) : nk_rgb(126, 132, 148));
                const struct ::nk_rect lineRect = DockSplitterOps::GetDockSplitterLineRect(node, boundaryIndex, thickness);

                const float overlapLeft = std::max(lineRect.x, clipRect.x);
                const float overlapTop = std::max(lineRect.y, clipRect.y);
                const float overlapRight = std::min(lineRect.x + lineRect.w, clipRect.x + clipRect.w);
                const float overlapBottom = std::min(lineRect.y + lineRect.h, clipRect.y + clipRect.h);
                if (overlapRight > overlapLeft && overlapBottom > overlapTop) {
                    nk_fill_rect(buffer,
                        nk_rect(overlapLeft, overlapTop, overlapRight - overlapLeft, overlapBottom - overlapTop),
                        0.0f,
                        lineColor);
                }
            }
        };

        drawSplits(root);
    }

    void ComputeDockNodeRects(Internal::DockNode* node, const struct ::nk_rect& parentRect) {
        NormalizeDockNodeLayoutForBounds(node, parentRect, 0.0f, 0.0f);
    }

} // namespace Spherical::DockLayoutOps

