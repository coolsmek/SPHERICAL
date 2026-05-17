#pragma once

#include "SphericalDockModel.h"
#include "docking/DockState.h"

#include <memory>

namespace Spherical::DockModelBridge {

    void SetFunctionTable(const DockModelFunctionTable* table);
    DockModelFunctionTable GetFunctionTable();

    bool ValidateWorkspaceContainerModel(const WorkspaceContainerModel& model);

    DockNodeModel ExportDockNodeModel(const Internal::DockNode* node);
    std::unique_ptr<Internal::DockNode> ImportDockNodeModel(const DockNodeModel& model);

    void ExportWorkspaceContainerState(const Internal::WorkspaceContainerState& state,
                                       WorkspaceContainerModel& outModel);
    void ApplyWorkspaceContainerModel(const WorkspaceContainerModel& model,
                                      Internal::WorkspaceContainerState& outState);

    bool SaveWorkspaceModel(const char* containerTitle, const WorkspaceContainerModel& model);
    bool LoadWorkspaceModel(const char* containerTitle, WorkspaceContainerModel& outModel);

} // namespace Spherical::DockModelBridge

