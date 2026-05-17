#include "docking/DockSplitterOps.h"

#include "docking/DockTreePrimitives.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

namespace {
    bool IsPointInsideRect(float x, float y, const struct ::nk_rect& rect) {
        return x >= rect.x && x <= (rect.x + rect.w) &&
               y >= rect.y && y <= (rect.y + rect.h);
    }
}

namespace Spherical::DockSplitterOps {

    struct ::nk_rect GetDockSplitterLineRect(const Internal::DockNode* node, std::size_t boundaryIndex, float thickness) {
        if (node == nullptr || DockTreePrimitives::IsDockTerminalNode(node) || thickness <= 0.0f || boundaryIndex + 1 >= node->children.size()) {
            return nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
        }

        const Internal::DockNode* leadingChild = node->children[boundaryIndex].get();
        if (leadingChild == nullptr) {
            return nk_rect(0.0f, 0.0f, 0.0f, 0.0f);
        }

        if (node->splitDirection == DockLayout::SideBySide) {
            const float dividerX = leadingChild->computedRect.x + leadingChild->computedRect.w;
            return nk_rect(dividerX - thickness * 0.5f, node->computedRect.y, thickness, node->computedRect.h);
        }

        const float dividerY = leadingChild->computedRect.y + leadingChild->computedRect.h;
        return nk_rect(node->computedRect.x, dividerY - thickness * 0.5f, node->computedRect.w, thickness);
    }

    struct ::nk_rect GetDockSplitterHitRect(const Internal::DockNode* node, std::size_t boundaryIndex, float thickness) {
        return GetDockSplitterLineRect(node, boundaryIndex, thickness);
    }

    bool DockTreeContainsNode(const Internal::DockNode* root, const Internal::DockNode* target) {
        if (root == nullptr || target == nullptr) {
            return false;
        }
        if (root == target) {
            return true;
        }

        for (const auto& child : root->children) {
            if (DockTreeContainsNode(child.get(), target)) {
                return true;
            }
        }
        return false;
    }

    bool IsValidDockSplitterRef(const Internal::DockNode* root, const Internal::DockSplitterRef& ref) {
        return root != nullptr &&
            ref.node != nullptr &&
            DockTreeContainsNode(root, ref.node) &&
            !DockTreePrimitives::IsDockTerminalNode(ref.node) &&
            ref.boundaryIndex + 1 < ref.node->children.size();
    }

    bool DockSplitterRefsEqual(const Internal::DockSplitterRef& a, const Internal::DockSplitterRef& b) {
        return a.node == b.node && a.boundaryIndex == b.boundaryIndex;
    }

    void PruneWorkspaceSplitterLinks(Internal::WorkspaceContainerState& containerState) {
        if (!containerState.root) {
            containerState.splitterLinkGroups.clear();
            return;
        }

        for (std::size_t groupIndex = 0; groupIndex < containerState.splitterLinkGroups.size();) {
            Internal::SplitterLinkGroup& group = containerState.splitterLinkGroups[groupIndex];
            std::vector<Internal::DockSplitterRef> filteredMembers;
            for (const Internal::DockSplitterRef& member : group.members) {
                if (!IsValidDockSplitterRef(containerState.root.get(), member)) {
                    continue;
                }
                if (member.node->splitDirection != group.splitDirection) {
                    continue;
                }

                const bool alreadyPresent = std::any_of(
                    filteredMembers.begin(),
                    filteredMembers.end(),
                    [&](const Internal::DockSplitterRef& existing) { return DockSplitterRefsEqual(existing, member); });
                if (!alreadyPresent) {
                    filteredMembers.push_back(member);
                }
            }

            if (filteredMembers.size() < 2) {
                containerState.splitterLinkGroups.erase(
                    containerState.splitterLinkGroups.begin() + static_cast<std::ptrdiff_t>(groupIndex));
                continue;
            }

            group.members = std::move(filteredMembers);
            ++groupIndex;
        }
    }

    void PruneWorkspaceSplitterIntersections(Internal::WorkspaceContainerState& containerState) {
        if (!containerState.root) {
            containerState.splitterIntersections.clear();
            return;
        }

        for (std::size_t intersectionIndex = 0; intersectionIndex < containerState.splitterIntersections.size();) {
            Internal::SplitterIntersectionState& intersection = containerState.splitterIntersections[intersectionIndex];

            auto filterMembers = [&](std::vector<Internal::DockSplitterRef>& members, DockLayout splitDirection) {
                std::vector<Internal::DockSplitterRef> filtered;
                for (const Internal::DockSplitterRef& member : members) {
                    if (!IsValidDockSplitterRef(containerState.root.get(), member) || member.node->splitDirection != splitDirection) {
                        continue;
                    }
                    const bool alreadyPresent = std::any_of(
                        filtered.begin(),
                        filtered.end(),
                        [&](const Internal::DockSplitterRef& existing) { return DockSplitterRefsEqual(existing, member); });
                    if (!alreadyPresent) {
                        filtered.push_back(member);
                    }
                }
                members = std::move(filtered);
            };

            filterMembers(intersection.verticalMembers, DockLayout::SideBySide);
            filterMembers(intersection.horizontalMembers, DockLayout::TopBottom);

            if (intersection.verticalMembers.empty() || intersection.horizontalMembers.empty()) {
                containerState.splitterIntersections.erase(
                    containerState.splitterIntersections.begin() + static_cast<std::ptrdiff_t>(intersectionIndex));
                continue;
            }

            ++intersectionIndex;
        }
    }

    void CollectDockSplitterSegmentGeometry(const Internal::DockNode* node,
                                           DockLayout splitDirection,
                                           std::vector<Internal::DockSplitterSegmentGeometry>& outSegments) {
        if (node == nullptr || DockTreePrimitives::IsDockTerminalNode(node)) {
            return;
        }

        for (const auto& child : node->children) {
            CollectDockSplitterSegmentGeometry(child.get(), splitDirection, outSegments);
        }

        if (node->splitDirection != splitDirection) {
            return;
        }

        const bool verticalSplitter = (splitDirection == DockLayout::SideBySide);
        for (std::size_t boundaryIndex = 0; boundaryIndex + 1 < node->children.size(); ++boundaryIndex) {
            const struct ::nk_rect lineRect = GetDockSplitterLineRect(node, boundaryIndex, 1.0f);
            outSegments.push_back({
                {const_cast<Internal::DockNode*>(node), boundaryIndex},
                verticalSplitter ? (lineRect.x + lineRect.w * 0.5f) : (lineRect.y + lineRect.h * 0.5f),
                verticalSplitter ? lineRect.y : lineRect.x,
                verticalSplitter ? (lineRect.y + lineRect.h) : (lineRect.x + lineRect.w)
            });
        }
    }

    std::vector<Internal::DockSplitterRef> CollectDockSplitterChain(const Internal::DockNode* root,
                                                                    DockLayout splitDirection,
                                                                    const std::vector<Internal::DockSplitterRef>& seedRefs) {
        std::vector<Internal::DockSplitterRef> chain;
        if (root == nullptr || seedRefs.empty()) {
            return chain;
        }

        std::vector<Internal::DockSplitterSegmentGeometry> allSegments;
        CollectDockSplitterSegmentGeometry(root, splitDirection, allSegments);
        if (allSegments.empty()) {
            return chain;
        }

        constexpr float kAxisEpsilon = 2.0f;
        constexpr float kSpanGapEpsilon = 2.0f;
        std::vector<bool> included(allSegments.size(), false);

        std::vector<std::size_t> frontier;
        for (const Internal::DockSplitterRef& seedRef : seedRefs) {
            for (std::size_t segmentIndex = 0; segmentIndex < allSegments.size(); ++segmentIndex) {
                if (included[segmentIndex]) {
                    continue;
                }
                if (!DockSplitterRefsEqual(allSegments[segmentIndex].ref, seedRef)) {
                    continue;
                }

                included[segmentIndex] = true;
                frontier.push_back(segmentIndex);
            }
        }

        while (!frontier.empty()) {
            const std::size_t currentIndex = frontier.back();
            frontier.pop_back();
            const Internal::DockSplitterSegmentGeometry& current = allSegments[currentIndex];

            for (std::size_t candidateIndex = 0; candidateIndex < allSegments.size(); ++candidateIndex) {
                if (included[candidateIndex]) {
                    continue;
                }

                const Internal::DockSplitterSegmentGeometry& candidate = allSegments[candidateIndex];
                if (std::fabs(candidate.axis - current.axis) > kAxisEpsilon) {
                    continue;
                }

                const float gap = std::max(current.spanMin, candidate.spanMin) - std::min(current.spanMax, candidate.spanMax);
                if (gap > kSpanGapEpsilon) {
                    continue;
                }

                included[candidateIndex] = true;
                frontier.push_back(candidateIndex);
            }
        }

        for (std::size_t segmentIndex = 0; segmentIndex < allSegments.size(); ++segmentIndex) {
            if (included[segmentIndex]) {
                chain.push_back(allSegments[segmentIndex].ref);
            }
        }

        return chain;
    }

    std::vector<Internal::DockSplitterRef> GetLinkedDockSplitterRefs(Internal::WorkspaceContainerState& containerState,
                                                                     Internal::DockNode* node,
                                                                     std::size_t boundaryIndex) {
        if (node == nullptr) {
            return {};
        }

        PruneWorkspaceSplitterLinks(containerState);
        PruneWorkspaceSplitterIntersections(containerState);
        const Internal::DockSplitterRef target{node, boundaryIndex};
        std::vector<Internal::DockSplitterRef> mergedRefs;
        auto appendUnique = [&](const Internal::DockSplitterRef& member) {
            if (!IsValidDockSplitterRef(containerState.root.get(), member)) {
                return;
            }
            const bool alreadyPresent = std::any_of(
                mergedRefs.begin(),
                mergedRefs.end(),
                [&](const Internal::DockSplitterRef& existing) { return DockSplitterRefsEqual(existing, member); });
            if (!alreadyPresent) {
                mergedRefs.push_back(member);
            }
        };

        for (const Internal::SplitterLinkGroup& group : containerState.splitterLinkGroups) {
            const auto match = std::find_if(
                group.members.begin(),
                group.members.end(),
                [&](const Internal::DockSplitterRef& member) { return DockSplitterRefsEqual(member, target); });
            if (match != group.members.end()) {
                for (const Internal::DockSplitterRef& member : group.members) {
                    appendUnique(member);
                }
            }
        }

        const DockLayout splitDirection = node->splitDirection;
        for (const Internal::SplitterIntersectionState& intersection : containerState.splitterIntersections) {
            const std::vector<Internal::DockSplitterRef>& members = (splitDirection == DockLayout::SideBySide)
                ? intersection.verticalMembers
                : intersection.horizontalMembers;
            const auto match = std::find_if(
                members.begin(),
                members.end(),
                [&](const Internal::DockSplitterRef& member) { return DockSplitterRefsEqual(member, target); });
            if (match == members.end()) {
                continue;
            }

            for (const Internal::DockSplitterRef& member : members) {
                appendUnique(member);
            }
        }

        if (mergedRefs.empty()) {
            mergedRefs.push_back(target);
        }
        return mergedRefs;
    }

    bool AreDockSplitterRefsLogicallyUnified(Internal::WorkspaceContainerState& containerState,
                                             DockLayout splitDirection,
                                             const Internal::DockSplitterRef& first,
                                             const Internal::DockSplitterRef& second) {
        if (splitDirection != DockLayout::SideBySide && splitDirection != DockLayout::TopBottom) {
            return false;
        }

        const std::vector<Internal::DockSplitterRef> firstRefs = GetLinkedDockSplitterRefs(containerState, first.node, first.boundaryIndex);
        const bool linkedMatch = std::any_of(
            firstRefs.begin(),
            firstRefs.end(),
            [&](const Internal::DockSplitterRef& member) { return DockSplitterRefsEqual(member, second); });
        if (linkedMatch) {
            return true;
        }

        PruneWorkspaceSplitterIntersections(containerState);
        for (const Internal::SplitterIntersectionState& intersection : containerState.splitterIntersections) {
            const std::vector<Internal::DockSplitterRef>& members = (splitDirection == DockLayout::SideBySide)
                ? intersection.verticalMembers
                : intersection.horizontalMembers;
            const bool hasFirst = std::any_of(members.begin(), members.end(), [&](const Internal::DockSplitterRef& member) {
                return DockSplitterRefsEqual(member, first);
            });
            const bool hasSecond = std::any_of(members.begin(), members.end(), [&](const Internal::DockSplitterRef& member) {
                return DockSplitterRefsEqual(member, second);
            });
            if (hasFirst && hasSecond) {
                return true;
            }
        }

        return false;
    }

    void RegisterLinkedSplitterGroup(Internal::WorkspaceContainerState& containerState,
                                     DockLayout splitDirection,
                                     std::vector<Internal::DockSplitterRef> members) {
        if (!containerState.root) {
            return;
        }

        std::vector<Internal::DockSplitterRef> mergedMembers;
        auto appendUnique = [&](const Internal::DockSplitterRef& member) {
            if (!IsValidDockSplitterRef(containerState.root.get(), member) || member.node->splitDirection != splitDirection) {
                return;
            }
            const bool alreadyPresent = std::any_of(
                mergedMembers.begin(),
                mergedMembers.end(),
                [&](const Internal::DockSplitterRef& existing) { return DockSplitterRefsEqual(existing, member); });
            if (!alreadyPresent) {
                mergedMembers.push_back(member);
            }
        };

        for (const Internal::DockSplitterRef& member : members) {
            appendUnique(member);
        }

        PruneWorkspaceSplitterLinks(containerState);
        for (std::size_t groupIndex = 0; groupIndex < containerState.splitterLinkGroups.size();) {
            Internal::SplitterLinkGroup& existingGroup = containerState.splitterLinkGroups[groupIndex];
            if (existingGroup.splitDirection != splitDirection) {
                ++groupIndex;
                continue;
            }

            const bool overlaps = std::any_of(
                existingGroup.members.begin(),
                existingGroup.members.end(),
                [&](const Internal::DockSplitterRef& existingMember) {
                    return std::any_of(
                        mergedMembers.begin(),
                        mergedMembers.end(),
                        [&](const Internal::DockSplitterRef& mergedMember) { return DockSplitterRefsEqual(existingMember, mergedMember); });
                });
            if (!overlaps) {
                ++groupIndex;
                continue;
            }

            for (const Internal::DockSplitterRef& existingMember : existingGroup.members) {
                appendUnique(existingMember);
            }
            containerState.splitterLinkGroups.erase(
                containerState.splitterLinkGroups.begin() + static_cast<std::ptrdiff_t>(groupIndex));
        }

        if (mergedMembers.size() >= 2) {
            containerState.splitterLinkGroups.push_back({splitDirection, std::move(mergedMembers)});
        }
    }

    void RegisterSplitterIntersection(Internal::WorkspaceContainerState& containerState,
                                     float x,
                                     float y,
                                     std::vector<Internal::DockSplitterRef> verticalMembers,
                                     std::vector<Internal::DockSplitterRef> horizontalMembers) {
        if (!containerState.root) {
            return;
        }

        auto filterMembers = [&](std::vector<Internal::DockSplitterRef>& members, DockLayout splitDirection) {
            std::vector<Internal::DockSplitterRef> filtered;
            for (const Internal::DockSplitterRef& member : members) {
                if (!IsValidDockSplitterRef(containerState.root.get(), member) || member.node->splitDirection != splitDirection) {
                    continue;
                }
                const bool alreadyPresent = std::any_of(
                    filtered.begin(),
                    filtered.end(),
                    [&](const Internal::DockSplitterRef& existing) { return DockSplitterRefsEqual(existing, member); });
                if (!alreadyPresent) {
                    filtered.push_back(member);
                }
            }
            members = std::move(filtered);
        };

        filterMembers(verticalMembers, DockLayout::SideBySide);
        filterMembers(horizontalMembers, DockLayout::TopBottom);
        if (verticalMembers.empty() || horizontalMembers.empty()) {
            return;
        }

        PruneWorkspaceSplitterIntersections(containerState);
        constexpr float kIntersectionEpsilon = 2.0f;
        for (Internal::SplitterIntersectionState& existing : containerState.splitterIntersections) {
            const bool closePoint = std::fabs(existing.x - x) <= kIntersectionEpsilon && std::fabs(existing.y - y) <= kIntersectionEpsilon;
            const bool overlapVertical = std::any_of(existing.verticalMembers.begin(), existing.verticalMembers.end(), [&](const Internal::DockSplitterRef& existingRef) {
                return std::any_of(verticalMembers.begin(), verticalMembers.end(), [&](const Internal::DockSplitterRef& newRef) {
                    return DockSplitterRefsEqual(existingRef, newRef);
                });
            });
            const bool overlapHorizontal = std::any_of(existing.horizontalMembers.begin(), existing.horizontalMembers.end(), [&](const Internal::DockSplitterRef& existingRef) {
                return std::any_of(horizontalMembers.begin(), horizontalMembers.end(), [&](const Internal::DockSplitterRef& newRef) {
                    return DockSplitterRefsEqual(existingRef, newRef);
                });
            });
            if (!(closePoint || (overlapVertical && overlapHorizontal))) {
                continue;
            }

            existing.x = x;
            existing.y = y;
            existing.verticalMembers.insert(existing.verticalMembers.end(), verticalMembers.begin(), verticalMembers.end());
            existing.horizontalMembers.insert(existing.horizontalMembers.end(), horizontalMembers.begin(), horizontalMembers.end());
            filterMembers(existing.verticalMembers, DockLayout::SideBySide);
            filterMembers(existing.horizontalMembers, DockLayout::TopBottom);
            return;
        }

        containerState.splitterIntersections.push_back({x, y, std::move(verticalMembers), std::move(horizontalMembers)});
    }

    Internal::DockSplitterHit FindDockSplitterAtPoint(Internal::DockNode* node,
                                                      float x,
                                                      float y,
                                                      float visualThickness,
                                                      float hitThickness) {
        Internal::DockSplitterHit result{};
        if (node == nullptr || DockTreePrimitives::IsDockTerminalNode(node)) {
            return result;
        }

        for (auto& child : node->children) {
            result = FindDockSplitterAtPoint(child.get(), x, y, visualThickness, hitThickness);
            if (result.node != nullptr) {
                return result;
            }
        }

        for (std::size_t boundaryIndex = 0; boundaryIndex + 1 < node->children.size(); ++boundaryIndex) {
            const struct ::nk_rect hitRect = GetDockSplitterHitRect(node, boundaryIndex, hitThickness);
            if (IsPointInsideRect(x, y, hitRect)) {
                return { node, boundaryIndex, GetDockSplitterLineRect(node, boundaryIndex, visualThickness), hitRect };
            }
        }

        return {};
    }

    Internal::DockNode* FindLeafAtPoint(Internal::DockNode* node, float x, float y) {
        if (node == nullptr) {
            return nullptr;
        }
        if (x < node->computedRect.x || x > node->computedRect.x + node->computedRect.w ||
            y < node->computedRect.y || y > node->computedRect.y + node->computedRect.h) {
            return nullptr;
        }
        if (node->type == Internal::DockNode::Type::Leaf) {
            return node;
        }
        for (auto& child : node->children) {
            if (Internal::DockNode* found = FindLeafAtPoint(child.get(), x, y)) {
                return found;
            }
        }
        return nullptr;
    }

    Internal::DropTargetState::DropZone DetermineDropZone(const struct ::nk_rect& leafRect, float x, float y) {
        const float w = leafRect.w;
        const float h = leafRect.h;
        const float marginW = w * 0.25f;
        const float marginH = h * 0.25f;

        const struct ::nk_rect centerRect = nk_rect(
            leafRect.x + marginW,
            leafRect.y + marginH,
            w * 0.5f,
            h * 0.5f
        );

        auto point_in_rect = [&](const struct ::nk_rect& rect) {
            return x >= rect.x && x <= rect.x + rect.w &&
                   y >= rect.y && y <= rect.y + rect.h;
        };

        auto point_in_convex_polygon = [&](const float* points, int pointCount) {
            if (pointCount < 3) {
                return false;
            }

            constexpr float kEpsilon = 0.001f;
            bool hasPositive = false;
            bool hasNegative = false;
            for (int i = 0; i < pointCount; ++i) {
                const int next = (i + 1) % pointCount;
                const float ax = points[i * 2 + 0];
                const float ay = points[i * 2 + 1];
                const float bx = points[next * 2 + 0];
                const float by = points[next * 2 + 1];
                const float cross = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
                if (cross > kEpsilon) {
                    hasPositive = true;
                } else if (cross < -kEpsilon) {
                    hasNegative = true;
                }

                if (hasPositive && hasNegative) {
                    return false;
                }
            }
            return true;
        };

        if (point_in_rect(centerRect)) {
            return Internal::DropTargetState::DropZone::Center;
        }

        const float topZonePoints[] = {
            leafRect.x, leafRect.y,
            leafRect.x + leafRect.w, leafRect.y,
            centerRect.x + centerRect.w, centerRect.y,
            centerRect.x, centerRect.y
        };
        const float leftZonePoints[] = {
            leafRect.x, leafRect.y,
            centerRect.x, centerRect.y,
            centerRect.x, centerRect.y + centerRect.h,
            leafRect.x, leafRect.y + leafRect.h
        };
        const float bottomZonePoints[] = {
            centerRect.x, centerRect.y + centerRect.h,
            centerRect.x + centerRect.w, centerRect.y + centerRect.h,
            leafRect.x + leafRect.w, leafRect.y + leafRect.h,
            leafRect.x, leafRect.y + leafRect.h
        };
        const float rightZonePoints[] = {
            centerRect.x + centerRect.w, centerRect.y,
            leafRect.x + leafRect.w, leafRect.y,
            leafRect.x + leafRect.w, leafRect.y + leafRect.h,
            centerRect.x + centerRect.w, centerRect.y + centerRect.h
        };

        if (point_in_convex_polygon(leftZonePoints, 4)) {
            return Internal::DropTargetState::DropZone::Left;
        }
        if (point_in_convex_polygon(rightZonePoints, 4)) {
            return Internal::DropTargetState::DropZone::Right;
        }
        if (point_in_convex_polygon(topZonePoints, 4)) {
            return Internal::DropTargetState::DropZone::Top;
        }
        if (point_in_convex_polygon(bottomZonePoints, 4)) {
            return Internal::DropTargetState::DropZone::Bottom;
        }

        return Internal::DropTargetState::DropZone::Center;
    }

} // namespace Spherical::DockSplitterOps

