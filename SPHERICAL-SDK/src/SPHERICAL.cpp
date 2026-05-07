#include "nuklear_config.h"
#include "SPHERICAL.h"
#include "VulkanRenderer.h"
#include "FontRenderer.h"
#include <algorithm>
#include <vector>
#include <cstring>
#include <chrono>
#include <array>
#include <cstddef>
#include <fstream>
#include <string>

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
    static std::vector<unsigned char> nk_cmd_buffer_storage;
    
    // Fallback fixed-width font for Nuklear layout (used until FreeType SDF atlas is complete)
    static nk_user_font s_fallbackFont = {};
    static float FallbackFontWidth(nk_handle handle, float height, const char* text, int len) {
        // Fixed-width approximation: ~7px per character at 13px height, scales linearly
        return len * (height * 0.54f);
    }
    
    // UI State
    namespace UIState {
        static float colorR = 0.9f;
        static float colorG = 0.2f;
        static float colorB = 0.2f;
        static int clickCounter = 0;
        static char textInput[65] = "Hello, World!";
        static int mouseX = 0;
        static int mouseY = 0;
        static double frameTime = 0.0;
        static std::chrono::high_resolution_clock::time_point lastFrameTime;
        static std::array<double, 60> frameTimes = {};
        static size_t frameIndex = 0;
        
        double GetFPS() {
            double totalMs = 0;
            for (double t : frameTimes) {
                totalMs += t;
            }
            double avgMs = totalMs / frameTimes.size();
            return avgMs > 0 ? 1000.0 / avgMs : 0.0;
        }
        
        void UpdateFrameTime() {
            auto now = std::chrono::high_resolution_clock::now();
            if (lastFrameTime.time_since_epoch().count() == 0) {
                lastFrameTime = now;
                return;
            }
            
            frameTime = std::chrono::duration<double, std::milli>(now - lastFrameTime).count();
            lastFrameTime = now;
            frameTimes[frameIndex] = frameTime;
            frameIndex = (frameIndex + 1) % frameTimes.size();
        }
    }
    
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
        
        // Initialize font renderer with bundled Roboto font.
        std::string resolvedFontPath;
        if (const char* basePathRaw = SDL_GetBasePath()) {
            std::string basePath(basePathRaw);
            const std::vector<std::string> fontCandidates = {
                basePath + "fonts/Roboto-VariableFont_wdth,wght.ttf",
                basePath + "../fonts/Roboto-VariableFont_wdth,wght.ttf",
                basePath + "../../../SPHERICAL-TEST/fonts/Roboto-VariableFont_wdth,wght.ttf",
                "SPHERICAL-TEST/fonts/Roboto-VariableFont_wdth,wght.ttf"
            };

            for (const auto& candidate : fontCandidates) {
                std::ifstream file(candidate.c_str(), std::ios::binary);
                if (file.good()) {
                    resolvedFontPath = candidate;
                    break;
                }
            }
        }

        const char* fontPath = resolvedFontPath.empty() ? nullptr : resolvedFontPath.c_str();
        if (!FontRenderer::Init(info.device, info.physicalDevice, 
                               info.graphicsQueue, info.commandPool,
                               fontPath, 24)) {
            VulkanRenderer::Shutdown();
            return false;
        }

        // Wire font atlas texture into Vulkan renderer's descriptor set
        VkImageView atlasView = FontRenderer::GetAtlasImageView();
        if (atlasView != VK_NULL_HANDLE) {
            VulkanRenderer::UpdateFontTexture(atlasView);
        }
         
        // Initialize Nuklear context
        static const size_t MAX_NUKLEAR_MEMORY = 16 * 1024 * 1024;  // 16 MB
        static const size_t MAX_NUKLEAR_DRAW_COMMAND_MEMORY = 4 * 1024 * 1024;  // 4 MB
        if (nk_buffer_storage.size() != MAX_NUKLEAR_MEMORY) {
            nk_buffer_storage.resize(MAX_NUKLEAR_MEMORY);
        }
        if (nk_cmd_buffer_storage.size() != MAX_NUKLEAR_DRAW_COMMAND_MEMORY) {
            nk_cmd_buffer_storage.resize(MAX_NUKLEAR_DRAW_COMMAND_MEMORY);
        }
        
        // Set up fallback font (fixed-width approximation used until FreeType SDF is ready)
        s_fallbackFont.height = 13.0f;
        s_fallbackFont.width = FallbackFontWidth;
        s_fallbackFont.userdata = nk_handle_ptr(nullptr);

        // Use real font handle from FontRenderer (now with baked glyphs)
        nk_user_font* fontToUse = reinterpret_cast<nk_user_font*>(FontRenderer::GetFontHandle());
        if (!fontToUse) {
            fontToUse = &s_fallbackFont;
        }
        
        nk_init_fixed(&ctx, nk_buffer_storage.data(), nk_buffer_storage.size(), fontToUse);
        
        // Initialize UI state
        UIState::lastFrameTime = std::chrono::high_resolution_clock::now();
        
        initialized = true;
        return true;
    }

    void NewFrame() {
        // Only proceed if properly initialized
        if (!initialized) {
            return;
        }
        
        // Update frame time tracking
        UIState::UpdateFrameTime();
        
        nk_input_begin(&ctx);
        
        // Poll and process SDL3 events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                // Mouse motion
                case SDL_EVENT_MOUSE_MOTION: {
                    int x = static_cast<int>(event.motion.x);
                    int y = static_cast<int>(event.motion.y);
                    UIState::mouseX = x;
                    UIState::mouseY = y;
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

    void SetRenderTarget(VkImageView colorAttachmentView, VkExtent2D framebufferExtent) {
        if (!initialized) {
            return;
        }
        VulkanRenderer::SetRenderTarget(colorAttachmentView, framebufferExtent);
    }
    
    // The 1.4 way: No RenderPass needed!
    void Render(VkCommandBuffer cmd) {
         // Only proceed if properly initialized
         if (!cmd || !initialized) {
             return;
         }

         const VkExtent2D framebufferExtent = VulkanRenderer::GetFramebufferExtent();
         const float width = static_cast<float>(framebufferExtent.width);
         const float height = static_cast<float>(framebufferExtent.height);
         if (width <= 0.0f || height <= 0.0f) {
             return;
         }
         
         // ====== NUKLEAR UI PANEL ======
         if (nk_begin(&ctx, "Control Panel", nk_rect(20, 20, 350, 550), NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
             // === Info Display Section ===
             nk_layout_row_dynamic(&ctx, 25, 1);
             {
                 char fps_label[64];
                 snprintf(fps_label, sizeof(fps_label), "FPS: %.1f", UIState::GetFPS());
                 nk_label(&ctx, fps_label, NK_TEXT_LEFT);
             }
             
             {
                 char mouse_label[64];
                 snprintf(mouse_label, sizeof(mouse_label), "Mouse: (%d, %d)", UIState::mouseX, UIState::mouseY);
                 nk_label(&ctx, mouse_label, NK_TEXT_LEFT);
             }
             
             {
                 char window_label[64];
                 snprintf(window_label, sizeof(window_label), "Window: %ux%u", framebufferExtent.width, framebufferExtent.height);
                 nk_label(&ctx, window_label, NK_TEXT_LEFT);
             }
             
             // Separator
             nk_layout_row_dynamic(&ctx, 10, 1);
             nk_spacing(&ctx, 1);
             
             // === Color Sliders Section ===
             nk_layout_row_dynamic(&ctx, 25, 2);
             nk_label(&ctx, "R:", NK_TEXT_LEFT);
             nk_slider_float(&ctx, 0.0f, &UIState::colorR, 1.0f, 0.01f);
             
             nk_layout_row_dynamic(&ctx, 25, 2);
             nk_label(&ctx, "G:", NK_TEXT_LEFT);
             nk_slider_float(&ctx, 0.0f, &UIState::colorG, 1.0f, 0.01f);
             
             nk_layout_row_dynamic(&ctx, 25, 2);
             nk_label(&ctx, "B:", NK_TEXT_LEFT);
             nk_slider_float(&ctx, 0.0f, &UIState::colorB, 1.0f, 0.01f);
             
             // Display current color value
             {
                 char color_label[64];
                 snprintf(color_label, sizeof(color_label), "Color: (%.2f, %.2f, %.2f)", 
                          UIState::colorR, UIState::colorG, UIState::colorB);
                 nk_layout_row_dynamic(&ctx, 25, 1);
                 nk_label(&ctx, color_label, NK_TEXT_LEFT);
             }
             
             // Separator
             nk_layout_row_dynamic(&ctx, 10, 1);
             nk_spacing(&ctx, 1);
             
             // === Button Section ===
             nk_layout_row_dynamic(&ctx, 30, 1);
             if (nk_button_label(&ctx, "Click Me")) {
                 UIState::clickCounter++;
             }
             
             {
                 char click_label[64];
                 snprintf(click_label, sizeof(click_label), "Clicks: %d", UIState::clickCounter);
                 nk_layout_row_dynamic(&ctx, 25, 1);
                 nk_label(&ctx, click_label, NK_TEXT_LEFT);
             }
             
             // Separator
             nk_layout_row_dynamic(&ctx, 10, 1);
             nk_spacing(&ctx, 1);
             
             // === Text Input Section ===
             nk_layout_row_dynamic(&ctx, 25, 1);
             nk_label(&ctx, "Text Input:", NK_TEXT_LEFT);
             
             nk_layout_row_dynamic(&ctx, 25, 1);
             nk_edit_string_zero_terminated(&ctx, NK_EDIT_FIELD, UIState::textInput, 
                                           sizeof(UIState::textInput), nk_filter_default);
             
             {
                 char text_label[128];
                 snprintf(text_label, sizeof(text_label), "Captured: %s", UIState::textInput);
                 nk_layout_row_dynamic(&ctx, 25, 1);
                 nk_label(&ctx, text_label, NK_TEXT_LEFT);
             }
         }
         nk_end(&ctx);
         
         // ====== CONVERT NUKLEAR COMMANDS TO VULKAN ======
         // Map vertex/index GPU buffers first
         void* vertPtr = nullptr;
         void* indexPtr = nullptr;
         size_t vertCapacity = 0;
         size_t indexCapacity = 0;
         
         if (!VulkanRenderer::MapVertexBuffer(&vertPtr, &vertCapacity)) {
             return;
         }
         if (!VulkanRenderer::MapIndexBuffer(&indexPtr, &indexCapacity)) {
             VulkanRenderer::UnmapBuffers();
             return;
         }
         
         // Create temporary Nuklear buffers for commands and GPU-backed vertices/indices
         nk_buffer cmds{};
         nk_buffer verts{};
         nk_buffer idxs{};
         nk_buffer_init_fixed(&cmds, nk_cmd_buffer_storage.data(), nk_cmd_buffer_storage.size());
         nk_buffer_init_fixed(&verts, vertPtr, vertCapacity);
         nk_buffer_init_fixed(&idxs, indexPtr, indexCapacity);

         static const nk_draw_vertex_layout_element vertexLayout[] = {
             {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct SphericalNkVertex, position)},
             {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct SphericalNkVertex, uv)},
             {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct SphericalNkVertex, col)},
             {NK_VERTEX_LAYOUT_END}
         };

         nk_draw_null_texture nullTexture{};
         nullTexture.texture = VulkanRenderer::GetNullTexture();
         nullTexture.uv = nk_vec2(0.5f / 512.0f, 0.5f / 512.0f);
         
         // Create convert config with fixed vertex layout
         nk_convert_config config{};
         config.global_alpha = 1.0f;
         config.shape_AA = NK_ANTI_ALIASING_ON;
         config.line_AA = NK_ANTI_ALIASING_ON;
         config.arc_segment_count = 22;
         config.circle_segment_count = 22;
         config.curve_segment_count = 22;
         config.vertex_layout = vertexLayout;
         config.vertex_size = sizeof(SphericalNkVertex);
         config.vertex_alignment = NK_ALIGNOF(struct SphericalNkVertex);
         config.tex_null = nullTexture;
         
         // Convert Nuklear commands to vertex/index buffers
         // nk_convert will write directly to the GPU memory we mapped
         const nk_flags convertResult = nk_convert(&ctx, &cmds, &verts, &idxs, &config);
         
         VulkanRenderer::UnmapBuffers();
         if (convertResult != NK_CONVERT_SUCCESS) {
             nk_clear(&ctx);
             return;
         }
         
         // ====== RENDER UI COMMANDS ======
         // Compute orthographic projection: maps pixel-space [0, 0] to [width, height] to NDC
         float proj[16];
         
         // Vulkan framebuffer coordinates with positive viewport height:
         // x: [0..width] -> [-1..1], y: [0..height] -> [-1..1] where y=0 is top.
         const float l = 0.0f;
         const float r = width;
         const float t = 0.0f;
         const float b = height;
         std::memset(proj, 0, sizeof(proj));
         proj[0] = 2.0f / (r - l);
         proj[5] = 2.0f / (b - t);
         proj[10] = -1.0f;
         proj[12] = -(r + l) / (r - l);
         proj[13] = -(b + t) / (b - t);
         proj[15] = 1.0f;
         
         // Begin UI rendering pass (starts vkCmdBeginRendering)
         VulkanRenderer::BeginUIPass(cmd, VK_NULL_HANDLE, proj);
         
         // Iterate over each draw command and submit scissored draws
         uint32_t indexOffset = 0;
         const nk_draw_command* drawCmd = nullptr;
         nk_draw_foreach(drawCmd, &ctx, &cmds) {
             if (drawCmd->elem_count == 0) {
                 continue;  // Skip empty commands
             }
             
             // Convert scissor rect from Nuklear pixel-space to Vulkan scissor coordinates
             int scissorX = static_cast<int>(drawCmd->clip_rect.x);
             int scissorY = static_cast<int>(drawCmd->clip_rect.y);
             int scissorW = static_cast<int>(drawCmd->clip_rect.w);
             int scissorH = static_cast<int>(drawCmd->clip_rect.h);
             
             // Clamp to framebuffer bounds
             scissorX = std::max(0, scissorX);
             scissorY = std::max(0, scissorY);
             scissorW = std::min(scissorW, static_cast<int>(width) - scissorX);
             scissorH = std::min(scissorH, static_cast<int>(height) - scissorY);
             
             if (scissorW > 0 && scissorH > 0) {
                 VulkanRenderer::DrawUICommand(cmd, drawCmd->elem_count, indexOffset,
                                              scissorX, scissorY, scissorW, scissorH);
             }
             
             indexOffset += drawCmd->elem_count;
         }
         
         // End UI rendering pass (ends vkCmdEndRendering)
         VulkanRenderer::EndUIPass(cmd);
         
         // Must be called each frame after rendering to reset Nuklear's internal sequence state
         nk_clear(&ctx);
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