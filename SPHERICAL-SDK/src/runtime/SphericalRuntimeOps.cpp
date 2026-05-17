#include "runtime/SphericalRuntimeOps.h"

#include "VulkanRenderer.h"
#include "FontRenderer.h"
#include "TaskRunner.h"
#include "backend/SphericalBackendOps.h"

#include "nuklear_config.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

namespace Spherical::Runtime {

    void RenderUiToCommandBuffer(nk_context& ctx,
                                 VkCommandBuffer cmd,
                                 std::vector<unsigned char>& nkCmdBufferStorage,
                                 const UiRenderHooks& hooks) {
        const VkExtent2D framebufferExtent = VulkanRenderer::GetFramebufferExtent();
        const float width = static_cast<float>(framebufferExtent.width);
        const float height = static_cast<float>(framebufferExtent.height);
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }

        if (hooks.drawCenterMarker != nullptr) {
            hooks.drawCenterMarker(&ctx, framebufferExtent, hooks.userData);
        }

        if (hooks.buildUi != nullptr) {
            hooks.buildUi(&ctx, framebufferExtent, hooks.userData);
        }

        if (hooks.drawDockPreview != nullptr) {
            hooks.drawDockPreview(&ctx, framebufferExtent, hooks.userData);
        }

        if (hooks.applyCursor != nullptr) {
            hooks.applyCursor(hooks.userData);
        }

        if (hooks.syncTextInput != nullptr) {
            hooks.syncTextInput(&ctx, hooks.userData);
        }

        void* vertPtr = nullptr;
        void* indexPtr = nullptr;
        size_t vertCapacity = 0;
        size_t indexCapacity = 0;

        if (!VulkanRenderer::MapVertexBuffer(&vertPtr, &vertCapacity)) {
            nk_clear(&ctx);
            return;
        }
        if (!VulkanRenderer::MapIndexBuffer(&indexPtr, &indexCapacity)) {
            VulkanRenderer::UnmapBuffers();
            nk_clear(&ctx);
            return;
        }

        nk_buffer cmds{};
        nk_buffer verts{};
        nk_buffer idxs{};
        nk_buffer_init_fixed(&cmds, nkCmdBufferStorage.data(), nkCmdBufferStorage.size());
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
        nullTexture.uv = nk_vec2(0.5f, 0.5f);

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

        const nk_flags convertResult = nk_convert(&ctx, &cmds, &verts, &idxs, &config);

        VulkanRenderer::UnmapBuffers();
        if (convertResult != NK_CONVERT_SUCCESS) {
            nk_clear(&ctx);
            return;
        }

        float proj[16];
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

        VulkanRenderer::BeginUIPass(cmd, VK_NULL_HANDLE, proj);

        uint32_t indexOffset = 0;
        const nk_draw_command* drawCmd = nullptr;
        nk_draw_foreach(drawCmd, &ctx, &cmds) {
            if (drawCmd->elem_count == 0) {
                continue;
            }

            int scissorX = static_cast<int>(drawCmd->clip_rect.x);
            int scissorY = static_cast<int>(drawCmd->clip_rect.y);
            int scissorW = static_cast<int>(drawCmd->clip_rect.w);
            int scissorH = static_cast<int>(drawCmd->clip_rect.h);

            scissorX = std::max(0, scissorX);
            scissorY = std::max(0, scissorY);
            scissorW = std::min(scissorW, static_cast<int>(width) - scissorX);
            scissorH = std::min(scissorH, static_cast<int>(height) - scissorY);

            if (scissorW > 0 && scissorH > 0) {
                VulkanRenderer::DrawUICommand(cmd, drawCmd->texture, drawCmd->elem_count, indexOffset,
                                              scissorX, scissorY, scissorW, scissorH);
            }

            indexOffset += drawCmd->elem_count;
        }

        VulkanRenderer::EndUIPass(cmd);
        nk_clear(&ctx);
    }

    void NewFrame(nk_context& ctx,
                  bool initialized,
                  int& mouseX,
                  int& mouseY,
                  const NewFrameHooks& hooks) {
        if (!initialized) {
            return;
        }

        if (hooks.setStage != nullptr) {
            hooks.setStage(StageId::NewFrameEvents, hooks.userData);
        }
        if (hooks.updateFrameTime != nullptr) {
            hooks.updateFrameTime(hooks.userData);
        }
        nk_input_begin(&ctx);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_MOUSE_MOTION: {
                    const int x = static_cast<int>(event.motion.x);
                    const int y = static_cast<int>(event.motion.y);
                    mouseX = x;
                    mouseY = y;
                    nk_input_motion(&ctx, x, y);
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    const int x = static_cast<int>(event.button.x);
                    const int y = static_cast<int>(event.button.y);

                    int button = NK_BUTTON_LEFT;
                    if (event.button.button == SDL_BUTTON_MIDDLE) {
                        button = NK_BUTTON_MIDDLE;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        button = NK_BUTTON_RIGHT;
                    }

                    const bool isDown = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                    nk_input_button(&ctx, static_cast<nk_buttons>(button), x, y, isDown);
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL: {
                    if (event.wheel.y != 0) {
                        nk_input_scroll(&ctx, nk_vec2(0, event.wheel.y * 5.0f));
                    }
                    if (event.wheel.x != 0) {
                        nk_input_scroll(&ctx, nk_vec2(event.wheel.x * 5.0f, 0));
                    }
                    break;
                }
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: {
                    const bool isDown = (event.type == SDL_EVENT_KEY_DOWN);
                    if (event.key.key == SDLK_LSHIFT || event.key.key == SDLK_RSHIFT) {
                        nk_input_key(&ctx, NK_KEY_SHIFT, isDown);
                    } else if (event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL) {
                        nk_input_key(&ctx, NK_KEY_CTRL, isDown);
                    } else if (event.key.key == SDLK_DELETE) {
                        nk_input_key(&ctx, NK_KEY_DEL, isDown);
                    } else if (event.key.key == SDLK_RETURN) {
                        nk_input_key(&ctx, NK_KEY_ENTER, isDown);
                    } else if (event.key.key == SDLK_TAB) {
                        nk_input_key(&ctx, NK_KEY_TAB, isDown);
                    } else if (event.key.key == SDLK_BACKSPACE) {
                        nk_input_key(&ctx, NK_KEY_BACKSPACE, isDown);
                    } else if (event.key.key == SDLK_UP) {
                        nk_input_key(&ctx, NK_KEY_UP, isDown);
                    } else if (event.key.key == SDLK_DOWN) {
                        nk_input_key(&ctx, NK_KEY_DOWN, isDown);
                    } else if (event.key.key == SDLK_LEFT) {
                        nk_input_key(&ctx, NK_KEY_LEFT, isDown);
                    } else if (event.key.key == SDLK_RIGHT) {
                        nk_input_key(&ctx, NK_KEY_RIGHT, isDown);
                    } else if (event.key.key == SDLK_HOME) {
                        nk_input_key(&ctx, NK_KEY_TEXT_START, isDown);
                    } else if (event.key.key == SDLK_END) {
                        nk_input_key(&ctx, NK_KEY_TEXT_END, isDown);
                    }
                    break;
                }
                case SDL_EVENT_TEXT_INPUT: {
                    nk_glyph glyph;
                    std::memset(glyph, 0, sizeof(glyph));
                    std::strncpy(reinterpret_cast<char*>(glyph), event.text.text, NK_UTF_SIZE - 1);
                    nk_input_glyph(&ctx, glyph);
                    break;
                }
                default:
                    break;
            }
        }

        nk_input_end(&ctx);
        if (hooks.setStage != nullptr) {
            hooks.setStage(StageId::NewFrameTaskPoll, hooks.userData);
        }
        if (hooks.taskPoll != nullptr) {
            hooks.taskPoll(hooks.userData);
        }
        if (hooks.setStage != nullptr) {
            hooks.setStage(StageId::Idle, hooks.userData);
        }
    }

    void RenderFrame(Backend::BackendState& backend,
                     bool initialized,
                     std::atomic<uint64_t>& frameCounter,
                     const RenderHooks& hooks) {
        if (!initialized || !backend.initialized) {
            return;
        }

        const uint64_t frameId = frameCounter.fetch_add(1, std::memory_order_relaxed) + 1;
        constexpr uint64_t kFenceWaitTimeoutNs = 200000000ULL;
        constexpr uint64_t kAcquireWaitTimeoutNs = 200000000ULL;

        if (backend.frameSubmissionInFlight) {
            if (hooks.setStage != nullptr) {
                hooks.setStage(StageId::WaitFence, hooks.userData);
            }
            const VkResult waitResult = vkWaitForFences(
                backend.device,
                1,
                &backend.inFlightFence,
                VK_TRUE,
                kFenceWaitTimeoutNs);
            if (waitResult == VK_TIMEOUT) {
                if (hooks.logSync != nullptr) {
                    hooks.logSync("wait_fence_timeout", waitResult, frameId, hooks.userData);
                }
                if (hooks.setStage != nullptr) {
                    hooks.setStage(StageId::Idle, hooks.userData);
                }
                return;
            }
            if (waitResult != VK_SUCCESS) {
                if (hooks.logSync != nullptr) {
                    hooks.logSync("wait_fence_failed", waitResult, frameId, hooks.userData);
                }
                if (hooks.setStage != nullptr) {
                    hooks.setStage(StageId::Idle, hooks.userData);
                }
                return;
            }
            backend.frameSubmissionInFlight = false;
        }

        if (hooks.setStage != nullptr) {
            hooks.setStage(StageId::AcquireImage, hooks.userData);
        }
        const VkResult acquireResult = vkAcquireNextImageKHR(
            backend.device,
            backend.swapchain,
            kAcquireWaitTimeoutNs,
            backend.imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &backend.currentImageIndex);

        if (acquireResult == VK_TIMEOUT) {
            if (hooks.logSync != nullptr) {
                hooks.logSync("acquire_timeout", acquireResult, frameId, hooks.userData);
            }
            if (hooks.setStage != nullptr) {
                hooks.setStage(StageId::Idle, hooks.userData);
            }
            return;
        }

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
            if (hooks.logSync != nullptr) {
                hooks.logSync("acquire_recreate_swapchain", acquireResult, frameId, hooks.userData);
            }
            Backend::RecreateSwapchain(backend);
            if (hooks.setStage != nullptr) {
                hooks.setStage(StageId::Idle, hooks.userData);
            }
            return;
        }
        if (acquireResult != VK_SUCCESS) {
            if (hooks.logSync != nullptr) {
                hooks.logSync("acquire_failed", acquireResult, frameId, hooks.userData);
            }
            if (hooks.setStage != nullptr) {
                hooks.setStage(StageId::Idle, hooks.userData);
            }
            return;
        }

        vkResetCommandBuffer(backend.commandBuffer, 0);
        if (!Backend::BeginFrameCommandBuffer(backend)) {
            return;
        }

        if (hooks.setStage != nullptr) {
            hooks.setStage(StageId::BuildUi, hooks.userData);
        }
        if (hooks.renderUi != nullptr) {
            hooks.renderUi(backend.commandBuffer, hooks.userData);
        }

        if (hooks.setStage != nullptr) {
            hooks.setStage(StageId::EndCommandBuffer, hooks.userData);
        }
        if (!Backend::EndFrameCommandBuffer(backend)) {
            if (hooks.setStage != nullptr) {
                hooks.setStage(StageId::Idle, hooks.userData);
            }
            return;
        }

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &backend.imageAvailableSemaphore;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &backend.commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &backend.renderFinishedSemaphore;

        if (hooks.setStage != nullptr) {
            hooks.setStage(StageId::Submit, hooks.userData);
        }
        if (vkResetFences(backend.device, 1, &backend.inFlightFence) != VK_SUCCESS) {
            if (hooks.logSync != nullptr) {
                hooks.logSync("reset_fence_failed", VK_ERROR_UNKNOWN, frameId, hooks.userData);
            }
            if (hooks.setStage != nullptr) {
                hooks.setStage(StageId::Idle, hooks.userData);
            }
            return;
        }

        if (vkQueueSubmit(backend.graphicsQueue, 1, &submitInfo, backend.inFlightFence) != VK_SUCCESS) {
            if (hooks.logSync != nullptr) {
                hooks.logSync("queue_submit_failed", VK_ERROR_UNKNOWN, frameId, hooks.userData);
            }
            if (hooks.setStage != nullptr) {
                hooks.setStage(StageId::Idle, hooks.userData);
            }
            return;
        }
        backend.frameSubmissionInFlight = true;

        if (hooks.setStage != nullptr) {
            hooks.setStage(StageId::Present, hooks.userData);
        }
        if (!Backend::PresentFrame(backend) && hooks.logSync != nullptr) {
            hooks.logSync("present_failed", VK_ERROR_UNKNOWN, frameId, hooks.userData);
        }
        if (hooks.setStage != nullptr) {
            hooks.setStage(StageId::Idle, hooks.userData);
        }
    }

} // namespace Spherical::Runtime

