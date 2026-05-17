#include "runtime/SphericalRuntimeState.h"

#include "TaskRunner.h"
#include "VulkanRenderer.h"

namespace {
    void RuntimeTaskPoll(void* /*userData*/) {
        Spherical::TaskRunner::Poll();
    }

    void RuntimeRenderUi(VkCommandBuffer cmd, void* userData) {
        if (userData == nullptr) {
            return;
        }

        auto* context = static_cast<Spherical::Runtime::RuntimeFacadeContext*>(userData);
        if (context->backend == nullptr || context->ctx == nullptr || context->nkCmdBufferStorage == nullptr) {
            return;
        }

        Spherical::VulkanRenderer::SetRenderTarget(
            context->backend->swapchainImageViews[context->backend->currentImageIndex],
            context->backend->swapchainExtent);

        Spherical::Runtime::RenderUiToCommandBuffer(
            *context->ctx,
            cmd,
            *context->nkCmdBufferStorage,
            context->uiHooks);
    }
}

namespace Spherical::Runtime {

    void PrimeFrameTimer(UiState& uiState) {
        uiState.lastFrameTime = std::chrono::high_resolution_clock::now();
    }

    double GetFPS(const UiState& uiState) {
        double totalMs = 0;
        for (double t : uiState.frameTimes) {
            totalMs += t;
        }
        const double avgMs = totalMs / static_cast<double>(uiState.frameTimes.size());
        return avgMs > 0 ? 1000.0 / avgMs : 0.0;
    }

    void UpdateFrameTime(UiState& uiState) {
        const auto now = std::chrono::high_resolution_clock::now();
        if (uiState.lastFrameTime.time_since_epoch().count() == 0) {
            uiState.lastFrameTime = now;
            return;
        }

        uiState.frameTime = std::chrono::duration<double, std::milli>(now - uiState.lastFrameTime).count();
        uiState.lastFrameTime = now;
        uiState.frameTimes[uiState.frameIndex] = uiState.frameTime;
        uiState.frameIndex = (uiState.frameIndex + 1) % uiState.frameTimes.size();
    }

    void RunNewFrame(RuntimeFacadeContext& context, bool initialized) {
        if (!initialized || context.ctx == nullptr || context.uiState == nullptr) {
            return;
        }

        UpdateFrameTime(*context.uiState);

        NewFrameHooks hooks{};
        hooks.userData = &context;
        hooks.setStage = context.setStage;
        hooks.taskPoll = RuntimeTaskPoll;
        NewFrame(*context.ctx, initialized, context.uiState->mouseX, context.uiState->mouseY, hooks);
    }

    void RunRender(RuntimeFacadeContext& context, bool initialized) {
        if (context.backend == nullptr || context.frameCounter == nullptr) {
            return;
        }

        RenderHooks hooks{};
        hooks.userData = &context;
        hooks.setStage = context.setStage;
        hooks.logSync = context.logSync;
        hooks.renderUi = RuntimeRenderUi;
        RenderFrame(*context.backend, initialized, *context.frameCounter, hooks);
    }

} // namespace Spherical::Runtime

