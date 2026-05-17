#pragma once

#include "SPHERICAL.h"
#include "backend/SphericalBackendState.h"

namespace Spherical::Backend {

    bool InitializeBackend(BackendState& state, const SphericalInitInfo& info);
    void ShutdownBackend(BackendState& state);

    bool RecreateSwapchain(BackendState& state);
    bool BeginFrameCommandBuffer(BackendState& state);
    bool EndFrameCommandBuffer(BackendState& state);
    bool PresentFrame(BackendState& state);

} // namespace Spherical::Backend

