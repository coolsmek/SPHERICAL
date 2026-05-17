#pragma once

#include "SPHERICAL.h"
#include "backend/SphericalBackendState.h"

#include <nuklear.h>

#include <vector>

namespace Spherical::Runtime {

    using ResolveUiScaleFn = float(*)(const SphericalInitInfo& info, SDL_Window* window, void* userData);
    using LifecycleHookFn = void(*)(void* userData);

    struct LifecycleHooks {
        void* userData = nullptr;
        ResolveUiScaleFn resolveUiScale = nullptr;
        LifecycleHookFn resetRuntimeDiagLog = nullptr;
        LifecycleHookFn startRenderWatchdog = nullptr;
        LifecycleHookFn stopRenderWatchdog = nullptr;
    };

    struct LifecycleState {
        Backend::BackendState* backend = nullptr;
        nk_context* ctx = nullptr;
        bool* initialized = nullptr;
        bool* textInputWasActive = nullptr;
        std::vector<unsigned char>* nkBufferStorage = nullptr;
        std::vector<unsigned char>* nkCmdBufferStorage = nullptr;
        nk_user_font* fallbackFont = nullptr;
    };

    bool InitLifecycle(const SphericalInitInfo& info,
                       LifecycleState& state,
                       const LifecycleHooks& hooks);

    void ShutdownLifecycle(LifecycleState& state,
                           const LifecycleHooks& hooks);

} // namespace Spherical::Runtime

