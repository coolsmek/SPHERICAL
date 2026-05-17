#include "docking/DockTreePrimitives.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
    constexpr float kDockedPanelPadding = Spherical::Internal::kDockedPanelPadding;
}

namespace Spherical::DockTreePrimitives {

    bool IsDockTerminalNode(const Internal::DockNode* node) {
        return node != nullptr && node->type == Internal::DockNode::Type::Leaf;
    }

    struct ::nk_rect InsetDockedPanelRect(const struct ::nk_rect& rect) {
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            return rect;
        }

        const float insetX = std::min(kDockedPanelPadding, rect.w * 0.5f);
        const float insetY = std::min(kDockedPanelPadding, rect.h * 0.5f);
        return nk_rect(
            rect.x + insetX,
            rect.y + insetY,
            std::max(0.0f, rect.w - insetX * 2.0f),
            std::max(0.0f, rect.h - insetY * 2.0f)
        );
    }

    float GetDockNodeMainAxisExtent(const Internal::DockNode* node, DockLayout splitDirection) {
        if (node == nullptr) {
            return 0.0f;
        }

        return (splitDirection == DockLayout::SideBySide)
            ? node->computedRect.w
            : node->computedRect.h;
    }

    float SumDockExtents(const std::vector<float>& extents) {
        float total = 0.0f;
        for (float extent : extents) {
            if (std::isfinite(extent) && extent > 0.0f) {
                total += extent;
            }
        }
        return total;
    }

    void EnsureDockSplitStorage(Internal::DockNode* node) {
        if (node == nullptr || IsDockTerminalNode(node)) {
            return;
        }

        const std::size_t childCount = node->children.size();
        if (node->preferredChildExtents.size() != childCount) {
            node->preferredChildExtents.resize(childCount, 0.0f);
        }
        if (node->computedChildExtents.size() != childCount) {
            node->computedChildExtents.resize(childCount, 0.0f);
        }

        for (std::size_t i = 0; i < childCount; ++i) {
            if (!(std::isfinite(node->preferredChildExtents[i]) && node->preferredChildExtents[i] > 0.0f)) {
                float fallbackExtent = GetDockNodeMainAxisExtent(node->children[i].get(), node->splitDirection);
                if (!(std::isfinite(fallbackExtent) && fallbackExtent > 0.0f)) {
                    fallbackExtent = 1.0f;
                }
                node->preferredChildExtents[i] = fallbackExtent;
            }
            if (!(std::isfinite(node->computedChildExtents[i]) && node->computedChildExtents[i] >= 0.0f)) {
                node->computedChildExtents[i] = 0.0f;
            }
        }
    }

    void RefreshDockGroupLegacyRatios(Internal::DockNode* node) {
        if (node == nullptr || IsDockTerminalNode(node)) {
            return;
        }

        EnsureDockSplitStorage(node);
        if (node->children.size() == 2) {
            const float totalPreferred = SumDockExtents(node->preferredChildExtents);
            node->splitRatio = (totalPreferred > 0.0f)
                ? std::clamp(node->preferredChildExtents[0] / totalPreferred, 0.0f, 1.0f)
                : 0.5f;
            node->preferredSplitRatio = node->splitRatio;
        }
    }

    std::unique_ptr<Internal::DockNode> BuildDockGroupNode(
        DockLayout splitDirection,
        std::vector<std::unique_ptr<Internal::DockNode>> children,
        std::vector<float> preferredChildExtents) {

        if (children.empty()) {
            return nullptr;
        }

        for (std::size_t i = 0; i < children.size();) {
            if (children[i] != nullptr) {
                ++i;
                continue;
            }

            children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < preferredChildExtents.size()) {
                preferredChildExtents.erase(preferredChildExtents.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }

        if (children.empty()) {
            return nullptr;
        }
        if (children.size() == 1) {
            return std::move(children.front());
        }

        auto node = std::make_unique<Internal::DockNode>();
        node->type = Internal::DockNode::Type::Split;
        node->splitDirection = splitDirection;
        node->children = std::move(children);
        node->preferredChildExtents = std::move(preferredChildExtents);
        node->computedChildExtents.assign(node->children.size(), 0.0f);
        EnsureDockSplitStorage(node.get());
        RefreshDockGroupLegacyRatios(node.get());
        return node;
    }

    void FlattenSameAxisChildGroups(Internal::DockNode* node) {
        if (node == nullptr || IsDockTerminalNode(node)) {
            return;
        }

        for (auto& child : node->children) {
            FlattenSameAxisChildGroups(child.get());
        }

        EnsureDockSplitStorage(node);
        for (std::size_t i = 0; i < node->children.size();) {
            Internal::DockNode* child = node->children[i].get();
            if (child == nullptr || IsDockTerminalNode(child) || child->splitDirection != node->splitDirection) {
                ++i;
                continue;
            }

            EnsureDockSplitStorage(child);

            float parentExtent = 0.0f;
            if (i < node->preferredChildExtents.size()) {
                parentExtent = node->preferredChildExtents[i];
            }
            if (!(std::isfinite(parentExtent) && parentExtent > 0.0f)) {
                parentExtent = SumDockExtents(child->preferredChildExtents);
            }
            if (!(std::isfinite(parentExtent) && parentExtent > 0.0f)) {
                parentExtent = static_cast<float>(std::max<std::size_t>(1, child->children.size()));
            }

            std::vector<float> insertedExtents = child->preferredChildExtents;
            const float insertedTotal = SumDockExtents(insertedExtents);
            if (insertedExtents.size() != child->children.size()) {
                insertedExtents.assign(child->children.size(), 1.0f);
            } else if (insertedTotal > 0.0f) {
                const float scale = parentExtent / insertedTotal;
                for (float& extent : insertedExtents) {
                    extent *= scale;
                }
            } else {
                insertedExtents.assign(child->children.size(), parentExtent / static_cast<float>(std::max<std::size_t>(1, child->children.size())));
            }

            auto flattenedChild = std::move(node->children[i]);
            node->children.erase(node->children.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < node->preferredChildExtents.size()) {
                node->preferredChildExtents.erase(node->preferredChildExtents.begin() + static_cast<std::ptrdiff_t>(i));
            }
            if (i < node->computedChildExtents.size()) {
                node->computedChildExtents.erase(node->computedChildExtents.begin() + static_cast<std::ptrdiff_t>(i));
            }

            for (std::size_t childIndex = 0; childIndex < flattenedChild->children.size(); ++childIndex) {
                node->children.insert(
                    node->children.begin() + static_cast<std::ptrdiff_t>(i + childIndex),
                    std::move(flattenedChild->children[childIndex]));
                node->preferredChildExtents.insert(
                    node->preferredChildExtents.begin() + static_cast<std::ptrdiff_t>(i + childIndex),
                    insertedExtents[childIndex]);
                node->computedChildExtents.insert(
                    node->computedChildExtents.begin() + static_cast<std::ptrdiff_t>(i + childIndex),
                    0.0f);
            }
        }

        EnsureDockSplitStorage(node);
        RefreshDockGroupLegacyRatios(node);
    }

    int CountDockNodeLeaves(const Internal::DockNode* node) {
        if (node == nullptr) {
            return 0;
        }
        if (node->type == Internal::DockNode::Type::Leaf) {
            return 1;
        }
        int total = 0;
        for (const auto& child : node->children) {
            total += CountDockNodeLeaves(child.get());
        }
        return total;
    }

    Internal::DockNode* FindDockNodeByPanelTitle(Internal::DockNode* node, const std::string& panelTitle) {
        if (node == nullptr) {
            return nullptr;
        }
        if (node->type == Internal::DockNode::Type::Leaf && node->panelTitle == panelTitle) {
            return node;
        }
        for (auto& child : node->children) {
            Internal::DockNode* found = FindDockNodeByPanelTitle(child.get(), panelTitle);
            if (found != nullptr) {
                return found;
            }
        }
        return nullptr;
    }

} // namespace Spherical::DockTreePrimitives


