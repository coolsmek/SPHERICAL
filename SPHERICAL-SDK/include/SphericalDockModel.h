#pragma once

#include "SphericalUI.h"

#include <vector>
#include <string>

namespace Spherical {

    struct DockNodeModel {
        enum class Type {
            Leaf,
            Split
        };

        Type type = Type::Leaf;
        std::string panelTitle;
        DockLayout splitDirection = DockLayout::SideBySide;
        std::vector<float> childExtents;
        std::vector<DockNodeModel> children;
    };

    struct WorkspaceContainerModel {
        bool hasRoot = false;
        bool needsRebalance = false;
        bool pendingSplitterReconcile = false;

        float offsetFromCenterX = 0.0f;
        float offsetFromCenterY = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        DockNodeModel root;
    };

    using DockModelSaveFn = bool(*)(const char* containerTitle,
                                    const WorkspaceContainerModel& model,
                                    void* userData);
    using DockModelLoadFn = bool(*)(const char* containerTitle,
                                    WorkspaceContainerModel& outModel,
                                    void* userData);
    using DockModelValidateFn = bool(*)(const WorkspaceContainerModel& model,
                                        void* userData);

    // Template-free callback surface for persistence/validation integrations.
    struct DockModelFunctionTable {
        void* userData = nullptr;
        DockModelSaveFn save = nullptr;
        DockModelLoadFn load = nullptr;
        DockModelValidateFn validate = nullptr;
    };

} // namespace Spherical

