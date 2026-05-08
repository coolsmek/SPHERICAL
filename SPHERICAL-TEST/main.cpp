#include <iostream>
#include <atomic>
#include <SDL3/SDL.h>
#include "SPHERICAL.h"

namespace {
    std::atomic_bool g_shouldQuit = false;

    bool SDLCALL EventWatch(void* userdata, SDL_Event* event) {
        (void)userdata;
        if (event == nullptr) {
            return true;
        }
        if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            g_shouldQuit.store(true);
        }
        return true;
    }
}

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "=== SPHERICAL Interactive Control Panel Demo ===" << std::endl;

    SDL_SetHint("SDL_VIDEODRIVER", "windows");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "Failed to initialize SDL3: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_AddEventWatch(EventWatch, nullptr);

    SDL_Window* window = SDL_CreateWindow(
        "SPHERICAL Control Panel",
        1280,
        720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    if (window == nullptr) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        SDL_RemoveEventWatch(EventWatch, nullptr);
        SDL_Quit();
        return 1;
    }

    Spherical::SphericalInitInfo initInfo{};
    initInfo.window = window;
    initInfo.preferImmediatePresent = true;
    initInfo.framesInFlight = 1;
    initInfo.enableValidation = false;

    if (!Spherical::Init(initInfo)) {
        std::cerr << "Failed to initialize SPHERICAL SDK!" << std::endl;
        SDL_DestroyWindow(window);
        SDL_RemoveEventWatch(EventWatch, nullptr);
        SDL_Quit();
        return 1;
    }

    while (!g_shouldQuit.load()) {
        Spherical::NewFrame();
        Spherical::Render();
    }

    Spherical::Shutdown();
    SDL_DestroyWindow(window);
    SDL_RemoveEventWatch(EventWatch, nullptr);
    SDL_Quit();

    std::cout << "✓ Application shutdown cleanly" << std::endl;
    return 0;
}
