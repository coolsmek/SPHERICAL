#include "docking/DockModelBridge.h"

#include <algorithm>
#include <cmath>

namespace {
    Spherical::DockModelFunctionTable g_dockModelFunctionTable{};

    bool IsFiniteModelExtent(float extent) {
        return std::isfinite(extent) && extent >= 0.0f;
    }

    bool ValidateDockNodeModel(const Spherical::DockNodeModel& model) {
        if (model.type == Spherical::DockNodeModel::Type::Leaf) {
            return model.children.empty() && model.childExtents.empty();
        }

        if (model.children.size() < 2) {
            return false;
        }

        if (!model.childExtents.empty() && model.childExtents.size() != model.children.size()) {
            return false;
        }

        for (float extent : model.childExtents) {
            if (!IsFiniteModelExtent(extent)) {
                return false;
            }
        }

        for (const Spherical::DockNodeModel& child : model.children) {
            if (!ValidateDockNodeModel(child)) {
                return false;
            }
        }

        return true;
    }

    void RefreshLegacyRatios(Spherical::Internal::DockNode* node) {
        if (node == nullptr || node->type == Spherical::Internal::DockNode::Type::Leaf) {
            return;
        }

        if (node->preferredChildExtents.size() != node->children.size()) {
            node->preferredChildExtents.assign(node->children.size(), 1.0f);
        }
        if (node->computedChildExtents.size() != node->children.size()) {
            node->computedChildExtents.assign(node->children.size(), 0.0f);
        }

        if (node->children.size() == 2) {
            const float left = std::max(0.0f, node->preferredChildExtents[0]);
            const float right = std::max(0.0f, node->preferredChildExtents[1]);
            const float total = left + right;
            node->splitRatio = (total > 0.0f) ? std::clamp(left / total, 0.0f, 1.0f) : 0.5f;
            node->preferredSplitRatio = node->splitRatio;
        }
    }
}

namespace Spherical::DockModelBridge {

    void SetFunctionTable(const DockModelFunctionTable* table) {
        g_dockModelFunctionTable = (table != nullptr) ? *table : DockModelFunctionTable{};
    }

    DockModelFunctionTable GetFunctionTable() {
        return g_dockModelFunctionTable;
    }

    bool ValidateWorkspaceContainerModel(const WorkspaceContainerModel& model) {
        if (!std::isfinite(model.offsetFromCenterX) || !std::isfinite(model.offsetFromCenterY) ||
            !std::isfinite(model.width) || !std::isfinite(model.height) ||
            model.width < 0.0f || model.height < 0.0f) {
            return false;
        }

        if (!model.hasRoot) {
            return true;
        }

        return ValidateDockNodeModel(model.root);
    }

    DockNodeModel ExportDockNodeModel(const Internal::DockNode* node) {
        DockNodeModel model;
        if (node == nullptr) {
            return model;
        }

        if (node->type == Internal::DockNode::Type::Leaf) {
            model.type = DockNodeModel::Type::Leaf;
            model.panelTitle = node->panelTitle;
            return model;
        }

        model.type = DockNodeModel::Type::Split;
        model.splitDirection = node->splitDirection;
        model.children.reserve(node->children.size());

        if (node->preferredChildExtents.size() == node->children.size()) {
            model.childExtents.reserve(node->preferredChildExtents.size());
            for (float extent : node->preferredChildExtents) {
                model.childExtents.push_back(IsFiniteModelExtent(extent) ? extent : 1.0f);
            }
        }

        for (const auto& child : node->children) {
            model.children.push_back(ExportDockNodeModel(child.get()));
        }

        return model;
    }

    std::unique_ptr<Internal::DockNode> ImportDockNodeModel(const DockNodeModel& model) {
        auto node = std::make_unique<Internal::DockNode>();
        if (model.type == DockNodeModel::Type::Leaf) {
            node->type = Internal::DockNode::Type::Leaf;
            node->panelTitle = model.panelTitle;
            return node;
        }

        node->type = Internal::DockNode::Type::Split;
        node->splitDirection = model.splitDirection;
        node->children.reserve(model.children.size());
        for (const DockNodeModel& childModel : model.children) {
            node->children.push_back(ImportDockNodeModel(childModel));
        }

        if (model.childExtents.size() == model.children.size()) {
            node->preferredChildExtents = model.childExtents;
            for (float& extent : node->preferredChildExtents) {
                if (!IsFiniteModelExtent(extent) || extent <= 0.0f) {
                    extent = 1.0f;
                }
            }
        } else {
            node->preferredChildExtents.assign(node->children.size(), 1.0f);
        }
        node->computedChildExtents.assign(node->children.size(), 0.0f);
        RefreshLegacyRatios(node.get());
        return node;
    }

    void ExportWorkspaceContainerState(const Internal::WorkspaceContainerState& state,
                                       WorkspaceContainerModel& outModel) {
        outModel = {};
        outModel.needsRebalance = state.needsRebalance;
        outModel.pendingSplitterReconcile = state.pendingSplitterReconcile;
        outModel.offsetFromCenterX = state.offsetFromCenterX;
        outModel.offsetFromCenterY = state.offsetFromCenterY;
        outModel.width = state.width;
        outModel.height = state.height;

        if (state.root != nullptr) {
            outModel.hasRoot = true;
            outModel.root = ExportDockNodeModel(state.root.get());
        }
    }

    void ApplyWorkspaceContainerModel(const WorkspaceContainerModel& model,
                                      Internal::WorkspaceContainerState& outState) {
        outState.initialized = true;
        outState.needsRebalance = model.needsRebalance;
        outState.pendingUndockAll = false;
        outState.pendingSplitterReconcile = model.pendingSplitterReconcile;
        outState.offsetFromCenterX = model.offsetFromCenterX;
        outState.offsetFromCenterY = model.offsetFromCenterY;
        outState.width = model.width;
        outState.height = model.height;
        outState.headerHeight = 0.0f;
        outState.splitterFlash = {};
        outState.splitterLinkGroups.clear();
        outState.splitterIntersections.clear();

        if (model.hasRoot) {
            outState.root = ImportDockNodeModel(model.root);
        } else {
            outState.root.reset();
            outState.pendingSplitterReconcile = false;
        }
    }

    bool SaveWorkspaceModel(const char* containerTitle, const WorkspaceContainerModel& model) {
        if (g_dockModelFunctionTable.save == nullptr) {
            return false;
        }

        if (g_dockModelFunctionTable.validate != nullptr &&
            !g_dockModelFunctionTable.validate(model, g_dockModelFunctionTable.userData)) {
            return false;
        }

        return g_dockModelFunctionTable.save(containerTitle, model, g_dockModelFunctionTable.userData);
    }

    bool LoadWorkspaceModel(const char* containerTitle, WorkspaceContainerModel& outModel) {
        if (g_dockModelFunctionTable.load == nullptr ||
            containerTitle == nullptr || containerTitle[0] == '\0') {
            return false;
        }

        if (!g_dockModelFunctionTable.load(containerTitle, outModel, g_dockModelFunctionTable.userData)) {
            return false;
        }

        if (!ValidateWorkspaceContainerModel(outModel)) {
            return false;
        }

        if (g_dockModelFunctionTable.validate != nullptr &&
            !g_dockModelFunctionTable.validate(outModel, g_dockModelFunctionTable.userData)) {
            return false;
        }

        return true;
    }

} // namespace Spherical::DockModelBridge

