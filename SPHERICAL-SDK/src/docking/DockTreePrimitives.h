#pragma once

#include "docking/DockState.h"

#include <memory>
#include <string>
#include <vector>

namespace Spherical::DockTreePrimitives {

    bool IsDockTerminalNode(const Internal::DockNode* node);
    struct ::nk_rect InsetDockedPanelRect(const struct ::nk_rect& rect);
    float GetDockNodeMainAxisExtent(const Internal::DockNode* node, DockLayout splitDirection);
    float SumDockExtents(const std::vector<float>& extents);
    void EnsureDockSplitStorage(Internal::DockNode* node);
    void RefreshDockGroupLegacyRatios(Internal::DockNode* node);

    std::unique_ptr<Internal::DockNode> BuildDockGroupNode(
        DockLayout splitDirection,
        std::vector<std::unique_ptr<Internal::DockNode>> children,
        std::vector<float> preferredChildExtents);

    void FlattenSameAxisChildGroups(Internal::DockNode* node);

    int CountDockNodeLeaves(const Internal::DockNode* node);
    Internal::DockNode* FindDockNodeByPanelTitle(Internal::DockNode* node, const std::string& panelTitle);

} // namespace Spherical::DockTreePrimitives


