#pragma once

#include "runtime/SphericalRuntimeOps.h"

#include <array>
#include <atomic>
#include <chrono>
#include <vector>

namespace Spherical::Runtime {

    struct UiState {
        float colorR = 0.9f;
        float colorG = 0.2f;
        float colorB = 0.2f;
        int clickCounter = 0;
        char textInput[65] = "Hello, World!";
        int mouseX = 0;
        int mouseY = 0;
        double frameTime = 0.0;
        std::chrono::high_resolution_clock::time_point lastFrameTime{};
        std::array<double, 60> frameTimes = {};
        size_t frameIndex = 0;

        bool isLoadingProject = false;
        int projectsLoaded = 0;
        std::chrono::high_resolution_clock::time_point loadStartTime{};
    };

    void PrimeFrameTimer(UiState& uiState);
    double GetFPS(const UiState& uiState);
    void UpdateFrameTime(UiState& uiState);

    struct RuntimeFacadeContext {
        nk_context* ctx = nullptr;
        Backend::BackendState* backend = nullptr;
        std::vector<unsigned char>* nkCmdBufferStorage = nullptr;
        std::atomic<uint64_t>* frameCounter = nullptr;
        UiState* uiState = nullptr;
        UiRenderHooks uiHooks{};
        SetStageFn setStage = nullptr;
        LogSyncFn logSync = nullptr;
    };

    void RunNewFrame(RuntimeFacadeContext& context, bool initialized);
    void RunRender(RuntimeFacadeContext& context, bool initialized);

} // namespace Spherical::Runtime

