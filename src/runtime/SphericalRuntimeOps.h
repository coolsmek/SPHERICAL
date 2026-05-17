#pragma once

#include "backend/SphericalBackendState.h"
#include "nuklear_config.h"

#include <vulkan/vulkan.h>
#include <nuklear.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace Spherical::Runtime {

    enum class StageId : int {
        Idle = 0,
        NewFrameEvents,
        NewFrameTaskPoll,
        WaitFence,
        AcquireImage,
        BuildUi,
        EndCommandBuffer,
        Submit,
        Present
    };

    using SetStageFn = void(*)(StageId stage, void* userData);
    using LogSyncFn = void(*)(const char* stage, VkResult result, uint64_t frameId, void* userData);
    using OverlayFn = void(*)(nk_context* context, const VkExtent2D& framebufferExtent, void* userData);
    using BuildUiFn = void(*)(nk_context* context, const VkExtent2D& framebufferExtent, void* userData);
    using SyncTextInputFn = void(*)(nk_context* context, void* userData);
    using VoidFn = void(*)(void* userData);
    using RenderUiFn = void(*)(VkCommandBuffer cmd, void* userData);

    struct UiRenderHooks {
        void* userData = nullptr;
        OverlayFn drawCenterMarker = nullptr;
        BuildUiFn buildUi = nullptr;
        OverlayFn drawDockPreview = nullptr;
        VoidFn applyCursor = nullptr;
        SyncTextInputFn syncTextInput = nullptr;
    };

    struct NewFrameHooks {
        void* userData = nullptr;
        SetStageFn setStage = nullptr;
        VoidFn updateFrameTime = nullptr;
        VoidFn taskPoll = nullptr;
    };

    struct RenderHooks {
        void* userData = nullptr;
        SetStageFn setStage = nullptr;
        LogSyncFn logSync = nullptr;
        RenderUiFn renderUi = nullptr;
    };

    void RenderUiToCommandBuffer(nk_context& ctx,
                                 VkCommandBuffer cmd,
                                 std::vector<unsigned char>& nkCmdBufferStorage,
                                 const UiRenderHooks& hooks);

    void NewFrame(nk_context& ctx,
                  bool initialized,
                  int& mouseX,
                  int& mouseY,
                  const NewFrameHooks& hooks);

    void RenderFrame(Backend::BackendState& backend,
                     bool initialized,
                     std::atomic<uint64_t>& frameCounter,
                     const RenderHooks& hooks);

} // namespace Spherical::Runtime


