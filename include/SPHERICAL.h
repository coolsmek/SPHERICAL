#pragma once

/**
 * @file SPHERICAL.h
 * @brief Spherical UI Rendering Library
 * 
 * A Nuklear-based UI library integrated with Vulkan 1.4 dynamic rendering
 * and SDL3 for window management and input handling.
 */

#include <SDL3/SDL.h>
#include <cstdint>

namespace Spherical {

    /**
     * @struct SphericalInitInfo
     * @brief Initialization parameters for the Spherical library
     */
    struct SphericalInitInfo {
        SDL_Window* window = nullptr;            ///< SDL3 window handle for input and surface creation
        bool preferImmediatePresent = true;      ///< Try VK_PRESENT_MODE_IMMEDIATE_KHR first, fallback to FIFO
        uint32_t framesInFlight = 1;             ///< Reserved for future multi-frame sync (currently clamped to 1)
        bool enableValidation = false;           ///< Reserved for future validation-layers toggle
    };

    /**
     * @brief Initialize the Spherical library
     * @param info Initialization parameters
     * @return true if initialization succeeded, false otherwise
     */
    bool Init(const SphericalInitInfo& info);
    
    /**
     * @brief Begin a new frame, process input
     * @note Safe to call even if Init() has not been called
     */
    void NewFrame();
    
    /**
     * @brief Render the UI (SDK owns acquire/submit/present internally)
     * @note Safe to call even if Init() has not been called
     */
    void Render();
    
    /**
     * @brief Shutdown and cleanup resources
     * @note Safe to call multiple times
     */
    void Shutdown();

}