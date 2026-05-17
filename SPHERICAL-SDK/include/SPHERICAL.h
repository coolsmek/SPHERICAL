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
#include "SphericalUI.h"
#include "SphericalDockModel.h"

namespace Spherical {

    /**
     * @struct SphericalInitInfo
     * @brief Initialization parameters for the Spherical library
     */
    struct SphericalInitInfo {
        SDL_Window* window = nullptr;            ///< SDL3 window handle for input and surface creation
        const char* fontPath = nullptr;          ///< Optional path to a TrueType/OpenType font file used for UI text
        FontRenderMode fontRenderMode = FontRenderMode::MSDF; ///< MSDF by default for scalable text; Grayscale remains available for tiny hinted UI text
        float manualDpiScale = 0.0f;            ///< Optional manual DPI/content scale override. <= 0 uses SDL_GetWindowDisplayScale(window)
        bool preferImmediatePresent = true;      ///< Try VK_PRESENT_MODE_IMMEDIATE_KHR first, fallback to FIFO
        uint32_t framesInFlight = 1;             ///< Reserved for future multi-frame sync (currently clamped to 1)
        bool enableValidation = false;           ///< Reserved for future validation-layers toggle
    };

    /**
     * @brief Initialize the Spherical library
     * @param info Initialization parameters
     * @return true if initialization succeeded, false otherwise
     */
    SPHERICAL_API bool Init(const SphericalInitInfo& info);
    
    /**
     * @brief Begin a new frame, process input
     * @note Safe to call even if Init() has not been called
     */
    SPHERICAL_API void NewFrame();
    
    /**
     * @brief Render the UI (SDK owns acquire/submit/present internally)
     * @note Safe to call even if Init() has not been called
     */
    SPHERICAL_API void Render();
    
    /**
     * @brief Shutdown and cleanup resources
     * @note Safe to call multiple times
     */
    SPHERICAL_API void Shutdown();

    /**
     * @brief Register function-table callbacks used to save/load/validate workspace dock models.
     * @param table Callback table pointer. Pass nullptr to clear callbacks.
     */
    SPHERICAL_API void SetDockModelFunctionTable(const DockModelFunctionTable* table);

    /**
     * @brief Get the currently registered dock model function table.
     */
    SPHERICAL_API DockModelFunctionTable GetDockModelFunctionTable();

    /**
     * @brief Export one workspace container docking tree into a public model.
     * @return true when container exists and model export succeeds.
     */
    SPHERICAL_API bool ExportWorkspaceModel(const char* containerTitle, WorkspaceContainerModel& outModel);

    /**
     * @brief Import one workspace container docking tree from a public model.
     * @return true if model is valid and import succeeds.
     */
    SPHERICAL_API bool ImportWorkspaceModel(const char* containerTitle, const WorkspaceContainerModel& model);

    /**
     * @brief Save one workspace model using the registered function table.
     */
    SPHERICAL_API bool SaveWorkspaceModel(const char* containerTitle);

    /**
     * @brief Load one workspace model using the registered function table.
     */
    SPHERICAL_API bool LoadWorkspaceModel(const char* containerTitle);

}