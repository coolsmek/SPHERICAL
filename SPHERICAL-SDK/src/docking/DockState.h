#pragma once

#include "nuklear_config.h"
#include <nuklear.h>
#include "SphericalUI.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Spherical::Internal {

    struct DockNode {
        enum class Type { Leaf, Split };

        Type type = Type::Leaf;
        std::string panelTitle;
        DockLayout splitDirection = DockLayout::SideBySide;
        float splitRatio = 0.5f;
        float preferredSplitRatio = 0.5f;
        std::vector<std::unique_ptr<DockNode>> children;
        std::vector<float> preferredChildExtents;
        std::vector<float> computedChildExtents;

        struct ::nk_rect computedRect = {};
    };

    struct SplitterDragState {
        bool active = false;
        std::string containerTitle;
        DockNode* splitNode = nullptr;
        std::size_t boundaryIndex = 0;
    };

    struct SplitterFlashState {
        bool active = false;
        DockNode* splitNode = nullptr;
        std::size_t boundaryIndex = 0;
        std::chrono::steady_clock::time_point startedAt{};
    };

    struct DockSplitterRef {
        DockNode* node = nullptr;
        std::size_t boundaryIndex = 0;
    };

    struct SplitterLinkGroup {
        DockLayout splitDirection = DockLayout::SideBySide;
        std::vector<DockSplitterRef> members;
    };

    struct SplitterIntersectionState {
        float x = 0.0f;
        float y = 0.0f;
        std::vector<DockSplitterRef> verticalMembers;
        std::vector<DockSplitterRef> horizontalMembers;
    };

    struct DockSplitterSegmentGeometry {
        DockSplitterRef ref;
        float axis = 0.0f;
        float spanMin = 0.0f;
        float spanMax = 0.0f;
    };

    struct WorkspaceContainerState {
        bool initialized = false;
        bool needsRebalance = false;
        bool pendingUndockAll = false;
        bool pendingSplitterReconcile = false;
        bool disallowUndock = false;
        float offsetFromCenterX = 0.0f;
        float offsetFromCenterY = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float headerHeight = 0.0f;
        SplitterFlashState splitterFlash;
        std::vector<SplitterLinkGroup> splitterLinkGroups;
        std::vector<SplitterIntersectionState> splitterIntersections;

        std::unique_ptr<DockNode> root;
    };

    struct DropTargetState {
        bool active = false;
        std::string containerTitle;
        DockNode* hoveredLeaf = nullptr;

        enum class DropZone { Left, Right, Top, Bottom, Center };
        DropZone zone = DropZone::Center;
    };

    struct DockSplitterHit {
        DockNode* node = nullptr;
        std::size_t boundaryIndex = 0;
        struct ::nk_rect lineRect{};
        struct ::nk_rect hitRect{};
    };

    inline constexpr float kDockSplitterVisualThickness = 3.0f;
    inline constexpr float kDockSplitterHitThickness = 5.0f;
    inline constexpr float kDockedPanelPadding = kDockSplitterVisualThickness;

} // namespace Spherical::Internal




