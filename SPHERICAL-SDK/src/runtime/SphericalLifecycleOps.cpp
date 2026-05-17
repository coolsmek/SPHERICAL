#include "runtime/SphericalLifecycleOps.h"

#include "FontRenderer.h"
#include "TaskRunner.h"
#include "VulkanRenderer.h"
#include "backend/SphericalBackendOps.h"

#include <cmath>

namespace Spherical::Runtime {

    bool InitLifecycle(const SphericalInitInfo& info,
                       LifecycleState& state,
                       const LifecycleHooks& hooks) {
        if (state.initialized == nullptr || state.backend == nullptr || state.ctx == nullptr ||
            state.nkBufferStorage == nullptr || state.nkCmdBufferStorage == nullptr ||
            state.fallbackFont == nullptr || state.textInputWasActive == nullptr) {
            return false;
        }

        if (*state.initialized || info.window == nullptr) {
            return false;
        }

        if (!Backend::InitializeBackend(*state.backend, info)) {
            Backend::ShutdownBackend(*state.backend);
            return false;
        }

        VulkanRenderer::RendererInitInfo rendererInfo{};
        rendererInfo.device = state.backend->device;
        rendererInfo.physicalDevice = state.backend->physicalDevice;
        rendererInfo.graphicsQueue = state.backend->graphicsQueue;
        rendererInfo.commandPool = state.backend->commandPool;
        rendererInfo.colorAttachmentFormat = state.backend->swapchainFormat;
        rendererInfo.colorAttachmentView = state.backend->swapchainImageViews.empty() ? VK_NULL_HANDLE : state.backend->swapchainImageViews[0];
        rendererInfo.framebufferExtent = state.backend->swapchainExtent;

        if (!VulkanRenderer::Init(rendererInfo)) {
            Backend::ShutdownBackend(*state.backend);
            return false;
        }

        std::string resolvedFontPath;
        if (info.fontPath != nullptr && info.fontPath[0] != '\0') {
            resolvedFontPath = info.fontPath;
        }

        const char* fontPath = resolvedFontPath.empty() ? nullptr : resolvedFontPath.c_str();
        float uiScale = 1.0f;
        if (hooks.resolveUiScale != nullptr) {
            uiScale = hooks.resolveUiScale(info, state.backend->window, hooks.userData);
        }

        if (!FontRenderer::Init(state.backend->device, state.backend->physicalDevice,
                                state.backend->graphicsQueue, state.backend->commandPool,
                                fontPath, uiScale, info.fontRenderMode)) {
            VulkanRenderer::Shutdown();
            Backend::ShutdownBackend(*state.backend);
            return false;
        }

        const VkImageView atlasView = FontRenderer::GetAtlasImageView();
        if (atlasView != VK_NULL_HANDLE) {
            VulkanRenderer::UpdateFontTexture(atlasView);
        }

        static const size_t MAX_NUKLEAR_MEMORY = 16 * 1024 * 1024;
        static const size_t MAX_NUKLEAR_DRAW_COMMAND_MEMORY = 4 * 1024 * 1024;
        if (state.nkBufferStorage->size() != MAX_NUKLEAR_MEMORY) {
            state.nkBufferStorage->resize(MAX_NUKLEAR_MEMORY);
        }
        if (state.nkCmdBufferStorage->size() != MAX_NUKLEAR_DRAW_COMMAND_MEMORY) {
            state.nkCmdBufferStorage->resize(MAX_NUKLEAR_DRAW_COMMAND_MEMORY);
        }

        state.fallbackFont->height = std::ceil(12.0f * uiScale);

        nk_user_font* fontToUse = FontRenderer::GetFontHandle(FontStyle::Regular);
        if (fontToUse == nullptr) {
            fontToUse = state.fallbackFont;
        }

        nk_init_fixed(state.ctx, state.nkBufferStorage->data(), state.nkBufferStorage->size(), fontToUse);
        if (state.backend->window != nullptr && SDL_TextInputActive(state.backend->window)) {
            SDL_StopTextInput(state.backend->window);
        }
        *state.textInputWasActive = false;

        if (hooks.resetRuntimeDiagLog != nullptr) {
            hooks.resetRuntimeDiagLog(hooks.userData);
        }
        TaskRunner::Init();
        if (hooks.startRenderWatchdog != nullptr) {
            hooks.startRenderWatchdog(hooks.userData);
        }

        *state.initialized = true;
        return true;
    }

    void ShutdownLifecycle(LifecycleState& state,
                           const LifecycleHooks& hooks) {
        if (state.backend == nullptr || state.ctx == nullptr ||
            state.initialized == nullptr || state.textInputWasActive == nullptr) {
            return;
        }

        if (*state.initialized) {
            nk_clear(state.ctx);
        }

        TaskRunner::Shutdown();
        if (state.backend->window != nullptr && SDL_TextInputActive(state.backend->window)) {
            SDL_StopTextInput(state.backend->window);
        }
        *state.textInputWasActive = false;

        if (hooks.stopRenderWatchdog != nullptr) {
            hooks.stopRenderWatchdog(hooks.userData);
        }

        FontRenderer::Shutdown();
        VulkanRenderer::Shutdown();
        Backend::ShutdownBackend(*state.backend);
    }

} // namespace Spherical::Runtime

