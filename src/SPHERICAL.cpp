#define NK_IMPLEMENTATION
#define NK_SDL3_VULKAN_IMPLEMENTATION
#include "SPHERICAL.h"
#include "VulkanRenderer.h"
#include "FontRenderer.h"
#include <nuklear.h>
#include <vector>
#include <cstring>

namespace {
    struct SimpleVertex {
        float x, y;
        float r, g, b;
    };
}

namespace Spherical {
    // Internal state
    static nk_context ctx = {};
    static bool initialized = false;
    static std::vector<unsigned char> nk_buffer_storage;
    
    bool Init(const SphericalInitInfo& info) {
        // Validate input parameters
        if (!info.instance || !info.device || !info.physicalDevice || !info.graphicsQueue ||
            !info.commandPool || !info.colorAttachmentView ||
            info.colorAttachmentFormat == VK_FORMAT_UNDEFINED ||
            info.framebufferExtent.width == 0 || info.framebufferExtent.height == 0 ||
            !info.window) {
            return false;
        }

        if (!VulkanRenderer::Init(info)) {
            return false;
        }
        
        // Initialize font renderer with FreeType
        if (!FontRenderer::Init(info.device, info.physicalDevice, 
                               info.graphicsQueue, info.commandPool,
                               nullptr, 32)) {
            VulkanRenderer::Shutdown();
            return false;
        }
         
         // Initialize Nuklear context (stack-allocated)
        // Note: We'll use a fixed memory buffer for Nuklear
        static const size_t MAX_NUKLEAR_MEMORY = 16 * 1024 * 1024;  // 16 MB
        if (nk_buffer_storage.size() != MAX_NUKLEAR_MEMORY) {
            nk_buffer_storage.resize(MAX_NUKLEAR_MEMORY);
        }
        
        // Initialize Nuklear with fixed memory
        nk_init_fixed(&ctx, nk_buffer_storage.data(), nk_buffer_storage.size(), nullptr);
        
        // TODO: 1. Set up Vulkan Font Texture (using VK_KHR_dynamic_rendering logic)
        // TODO: 2. Setup SDL3 Input backend mapping
        
        initialized = true;
        return true;
    }

    void NewFrame() {
        // Only proceed if properly initialized
        if (!initialized) {
            return;
        }
        
        nk_input_begin(&ctx);
        
        // Poll and process SDL3 events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                // Mouse motion
                case SDL_EVENT_MOUSE_MOTION: {
                    int x = static_cast<int>(event.motion.x);
                    int y = static_cast<int>(event.motion.y);
                    nk_input_motion(&ctx, x, y);
                    break;
                }
                
                // Mouse buttons
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    int x = static_cast<int>(event.button.x);
                    int y = static_cast<int>(event.button.y);
                    int button = NK_BUTTON_LEFT;
                    
                    if (event.button.button == SDL_BUTTON_MIDDLE) {
                        button = NK_BUTTON_MIDDLE;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        button = NK_BUTTON_RIGHT;
                    }
                    
                    nk_input_button(&ctx, static_cast<nk_buttons>(button), x, y, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                    break;
                }
                
                // Mouse wheel / scroll
                case SDL_EVENT_MOUSE_WHEEL: {
                    if (event.wheel.y != 0) {
                        nk_input_scroll(&ctx, nk_vec2(0, event.wheel.y * 5.0f));
                    }
                    if (event.wheel.x != 0) {
                        nk_input_scroll(&ctx, nk_vec2(event.wheel.x * 5.0f, 0));
                    }
                    break;
                }
                
                // Key events
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: {
                    if (event.key.key == SDLK_LSHIFT || event.key.key == SDLK_RSHIFT) {
                        nk_input_key(&ctx, NK_KEY_SHIFT, event.type == SDL_EVENT_KEY_DOWN);
                    } else if (event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL) {
                        nk_input_key(&ctx, NK_KEY_CTRL, event.type == SDL_EVENT_KEY_DOWN);
                    } else if (event.key.key == SDLK_DELETE) {
                        nk_input_key(&ctx, NK_KEY_DEL, event.type == SDL_EVENT_KEY_DOWN);
                    } else if (event.key.key == SDLK_RETURN) {
                        nk_input_key(&ctx, NK_KEY_ENTER, event.type == SDL_EVENT_KEY_DOWN);
                    } else if (event.key.key == SDLK_TAB) {
                        nk_input_key(&ctx, NK_KEY_TAB, event.type == SDL_EVENT_KEY_DOWN);
                    } else if (event.key.key == SDLK_BACKSPACE) {
                        nk_input_key(&ctx, NK_KEY_BACKSPACE, event.type == SDL_EVENT_KEY_DOWN);
                    } else if (event.key.key == SDLK_UP) {
                        nk_input_key(&ctx, NK_KEY_UP, event.type == SDL_EVENT_KEY_DOWN);
                    } else if (event.key.key == SDLK_DOWN) {
                        nk_input_key(&ctx, NK_KEY_DOWN, event.type == SDL_EVENT_KEY_DOWN);
                    } else if (event.key.key == SDLK_LEFT) {
                        nk_input_key(&ctx, NK_KEY_LEFT, event.type == SDL_EVENT_KEY_DOWN);
                    } else if (event.key.key == SDLK_RIGHT) {
                        nk_input_key(&ctx, NK_KEY_RIGHT, event.type == SDL_EVENT_KEY_DOWN);
                    } else if (event.key.key == SDLK_HOME) {
                        nk_input_key(&ctx, NK_KEY_TEXT_START, event.type == SDL_EVENT_KEY_DOWN);
                    } else if (event.key.key == SDLK_END) {
                        nk_input_key(&ctx, NK_KEY_TEXT_END, event.type == SDL_EVENT_KEY_DOWN);
                    }
                    break;
                }
                
                // Text input
                case SDL_EVENT_TEXT_INPUT: {
                    nk_glyph glyph;
                    std::memcpy(glyph, event.text.text, NK_UTF_SIZE);
                    nk_input_glyph(&ctx, glyph);
                    break;
                }
                
                // Window events
                case SDL_EVENT_WINDOW_HIDDEN:
                case SDL_EVENT_WINDOW_MINIMIZED: {
                    // Could set a "minimized" flag if needed
                    break;
                }
                
                case SDL_EVENT_QUIT: {
                    // Application quit requested - just ignore here
                    // Host application is responsible for handling this
                    break;
                }
                
                default:
                    break;
            }
        }
        
        nk_input_end(&ctx);
    }
    
    // The 1.4 way: No RenderPass needed!
    void Render(VkCommandBuffer cmd) {
        // Only proceed if properly initialized
        if (!cmd || !initialized) {
            return;
        }
        
        const SimpleVertex vertices[] = {
            {0.0f, -0.5f, 0.9f, 0.2f, 0.2f},
            {0.5f, 0.5f, 0.2f, 0.9f, 0.2f},
            {-0.5f, 0.5f, 0.2f, 0.3f, 0.9f},
        };

        const uint16_t indices[] = {0, 1, 2};

        VulkanRenderer::SubmitGeometry(vertices, sizeof(vertices), indices, sizeof(indices));
        // TODO: Use vkCmdBeginRendering here for dynamic rendering
        VulkanRenderer::Render(cmd);
    }
    
    void Shutdown() {
        if (initialized) {
            nk_clear(&ctx);
        }
        FontRenderer::Shutdown();
        VulkanRenderer::Shutdown();
        initialized = false;
    }
}