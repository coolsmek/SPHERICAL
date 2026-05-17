#include "nuklear_config.h"
#include "SPHERICAL.h"
#include "VulkanRenderer.h"
#include "FontRenderer.h"
#include "TaskRunner.h"
#include "docking/DockState.h"
#include "docking/DockModelBridge.h"
#include "docking/DockTreePrimitives.h"
#include "docking/DockSplitterOps.h"
#include "docking/DockLayoutOps.h"
#include "docking/DockReconcileOps.h"
#include "docking/DockMutationOps.h"
#include "docking/DockOverlayOps.h"
#include "backend/SphericalBackendState.h"
#include "backend/SphericalBackendOps.h"
#include "runtime/SphericalRuntimeOps.h"
#include "runtime/SphericalRuntimeState.h"
#include "runtime/SphericalLifecycleOps.h"
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>
#include <atomic>
#include <array>
#include <cstddef>
#include <unordered_map>
#include <fstream>
#include <string>
#include <iostream>
#include <sstream>
#include <mutex>
#include <optional>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
    using namespace Spherical::Internal;

    using BackendState = Spherical::Backend::BackendState;
    
    struct ScrollbarDragState {
        bool active = false;
        std::string windowTitle;
        float dragStartMouseY = 0.0f;
        float dragStartScrollY = 0.0f;
        float trackY = 0.0f;
        float trackH = 0.0f;
        float thumbH = 0.0f;
        float maxScrollY = 0.0f;
        float grabOffsetY = 0.0f;  // distance from mouse to top of thumb at click time
    };

    struct SliderTrackDragState {
        bool active = false;
        float* valueRef = nullptr;
    };

    enum class PanelDragMode {
        None,
        Move,
        ResizeTopLeft,
        ResizeTopRight,
        ResizeBottomLeft,
        ResizeBottomRight,
        ResizeTop,
        ResizeRight,
        ResizeBottom,
        ResizeLeft
    };

    struct PanelDragState {
        bool active = false;
        std::string windowTitle;
        PanelDragMode mode = PanelDragMode::None;
        float mouseStartX = 0.0f;
        float mouseStartY = 0.0f;
        float globalMouseStartX = 0.0f;
        float globalMouseStartY = 0.0f;
        struct nk_rect panelStartBounds{};
    };

    struct PanelPersistentState {
        bool initialized = false;
        float offsetFromCenterX = 0.0f;
        float offsetFromCenterY = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float initialOffsetFromCenterX = 0.0f;
        float initialOffsetFromCenterY = 0.0f;
        float initialWidth = 0.0f;
        float initialHeight = 0.0f;
    };

    struct PanelSubsectionPersistentState {
        bool initialized = false;
        bool expanded = true;
    };

    constexpr char kPanelSubsectionStatePanelSeparator = '\x1E';
    constexpr char kPanelSubsectionStatePathSeparator = '\x1F';

    static bool IsDockTerminalNode(const DockNode* node);
    static struct nk_rect InsetDockedPanelRect(const struct nk_rect& rect);
    static struct nk_rect GetDockSplitterLineRect(const DockNode* node, std::size_t boundaryIndex, float thickness = 2.0f);
    static struct nk_rect GetDockSplitterHitRect(const DockNode* node, std::size_t boundaryIndex, float thickness = 10.0f);
    static bool DockTreeContainsNode(const DockNode* root, const DockNode* target);
    static bool IsValidDockSplitterRef(const DockNode* root, const DockSplitterRef& ref);
    static void PruneWorkspaceSplitterLinks(WorkspaceContainerState& containerState);
    static void PruneWorkspaceSplitterIntersections(WorkspaceContainerState& containerState);
    static void CollectDockSplitterSegmentGeometry(const DockNode* node,
                                                  Spherical::DockLayout splitDirection,
                                                  std::vector<DockSplitterSegmentGeometry>& outSegments);
    static std::vector<DockSplitterRef> CollectDockSplitterChain(const DockNode* root,
                                                                 Spherical::DockLayout splitDirection,
                                                                 const std::vector<DockSplitterRef>& seedRefs);
    static std::vector<DockSplitterRef> GetLinkedDockSplitterRefs(WorkspaceContainerState& containerState,
                                                                  DockNode* node,
                                                                  std::size_t boundaryIndex);
    static bool AreDockSplitterRefsLogicallyUnified(WorkspaceContainerState& containerState,
                                                    Spherical::DockLayout splitDirection,
                                                    const DockSplitterRef& first,
                                                    const DockSplitterRef& second);
    static void RegisterLinkedSplitterGroup(WorkspaceContainerState& containerState,
                                            Spherical::DockLayout splitDirection,
                                            std::vector<DockSplitterRef> members);
    static void RegisterSplitterIntersection(WorkspaceContainerState& containerState,
                                            float x,
                                            float y,
                                            std::vector<DockSplitterRef> verticalMembers,
                                            std::vector<DockSplitterRef> horizontalMembers);
    static DockSplitterHit FindDockSplitterAtPoint(DockNode* node, float x, float y,
                                                   float visualThickness = 2.0f, float hitThickness = 10.0f);
    static bool GetDockSplitterClampRange(const DockNode* node,
                                          std::size_t boundaryIndex,
                                          float leafMinWidth,
                                          float leafMinHeight,
                                          float& outMinPosition,
                                          float& outMaxPosition);
    static float ClampDockSplitterPosition(const DockNode* node, std::size_t boundaryIndex, float proposedPosition,
                                          float leafMinWidth, float leafMinHeight);
    static float ClampLinkedDockSplitterPosition(WorkspaceContainerState& containerState,
                                                 DockNode* node,
                                                 std::size_t boundaryIndex,
                                                 float proposedPosition,
                                                 float leafMinWidth,
                                                 float leafMinHeight);
    static void AdjustDockSplitterToPosition(DockNode* node, std::size_t boundaryIndex, float proposedPosition,
                                             float leafMinWidth, float leafMinHeight);
    static bool TryUnifyDockSplitterBoundary(std::unique_ptr<DockNode>& root,
                                             WorkspaceContainerState& containerState,
                                             DockNode* activeNode,
                                             std::size_t activeBoundaryIndex,
                                             float snapThreshold,
                                             float leafMinWidth,
                                             float leafMinHeight,
                                             DockSplitterHit& outUnifiedHit);
    static bool CascadeWorkspaceContainerSplitterUnifications(std::unique_ptr<DockNode>& root,
                                                              WorkspaceContainerState& containerState,
                                                              const struct nk_rect& rootRect,
                                                              float snapThreshold,
                                                              float leafMinWidth,
                                                              float leafMinHeight);
    static void ReconcileWorkspaceContainerSplitters(std::unique_ptr<DockNode>& root,
                                                     WorkspaceContainerState& containerState,
                                                     const struct nk_rect& rootRect,
                                                     float leafMinWidth,
                                                     float leafMinHeight);
    static void CollapseDockGroups(std::unique_ptr<DockNode>& node);
    static void NormalizeDockNodeLayoutForBounds(DockNode* node, const struct nk_rect& parentRect,
                                                 float leafMinWidth, float leafMinHeight);
    static void DrawDockSplitLinesForClipRect(nk_command_buffer* buffer, const DockNode* root, const struct nk_rect& clipRect,
                                              const DockSplitterHit& activeHit, const DockSplitterHit& hoveredHit);
    static void DrawWorkspaceSplitterOverlay(nk_context* context, const VkExtent2D& framebufferExtent);

    enum class CursorRequest {
        Default,
        ResizeNwse,
        ResizeNesw,
        ResizeNs,
        ResizeEw
    };

    struct CursorState {
        CursorRequest requested = CursorRequest::Default;
        CursorRequest applied = CursorRequest::Default;
        SDL_Cursor* arrow = nullptr;
        SDL_Cursor* nwse = nullptr;
        SDL_Cursor* nesw = nullptr;
        SDL_Cursor* ns = nullptr;
        SDL_Cursor* ew = nullptr;
    };
    
    BackendState g_backend;
    bool g_textInputWasActive = false;

    // UI registration and implementation
    Spherical::UIBuildFn g_uiBuildCallback = nullptr;

    float GetWindowDisplayScale(SDL_Window* window) {
        if (window == nullptr) {
            return 1.0f;
        }

        const float scale = SDL_GetWindowDisplayScale(window);
        return scale > 0.0f ? scale : 1.0f;
    }

    float ResolveUiScale(const Spherical::SphericalInitInfo& info, SDL_Window* window) {
        if (info.manualDpiScale > 0.0f) {
            return info.manualDpiScale;
        }
        return GetWindowDisplayScale(window);
    }

    bool IsNuklearTextEditActive(const nk_context* context) {
        if (context == nullptr || context->active == nullptr) {
            return false;
        }

        if (context->active->popup.win != nullptr) {
            return context->active->popup.win->edit.active != 0;
        }

        return context->active->edit.active != 0;
    }

    void SyncWindowTextInputState(const nk_context* context) {
        if (g_backend.window == nullptr || context == nullptr) {
            return;
        }

        const bool editActive = IsNuklearTextEditActive(context);
        if (editActive == g_textInputWasActive) {
            return;
        }

        const bool windowTextInputActive = SDL_TextInputActive(g_backend.window);
        if (editActive && !windowTextInputActive) {
            SDL_StartTextInput(g_backend.window);
        } else if (!editActive && windowTextInputActive) {
            SDL_StopTextInput(g_backend.window);
        }

        g_textInputWasActive = editActive;
    }

    ScrollbarDragState g_scrollbarDrag;
    SliderTrackDragState g_sliderTrackDrag;
    PanelDragState g_panelDrag;
    SplitterDragState g_splitterDrag;
    std::unordered_map<std::string, PanelPersistentState> g_panelStates;
    std::unordered_map<std::string, PanelSubsectionPersistentState> g_panelSubsectionStates;
    std::unordered_map<std::string, WorkspaceContainerState> g_workspaceContainerStates;
    DropTargetState g_dropTargetState;
    CursorState g_cursorState;
    std::atomic<uint64_t> g_renderFrameCounter{0};
    std::chrono::steady_clock::time_point g_lastRenderSyncLogTime{};
    std::atomic<bool> g_renderWatchdogRunning{false};
    std::thread g_renderWatchdogThread;
    std::atomic<uint64_t> g_renderWatchdogSerial{0};
    std::atomic<int> g_renderWatchdogStage{0};
    std::mutex g_runtimeDiagMutex;
    std::string g_runtimeDiagLogPath;

    enum class RenderWatchdogStage : int {
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

    static const char* RenderWatchdogStageName(RenderWatchdogStage stage) {
        switch (stage) {
            case RenderWatchdogStage::Idle:
                return "idle";
            case RenderWatchdogStage::NewFrameEvents:
                return "new_frame_events";
            case RenderWatchdogStage::NewFrameTaskPoll:
                return "new_frame_task_poll";
            case RenderWatchdogStage::WaitFence:
                return "wait_fence";
            case RenderWatchdogStage::AcquireImage:
                return "acquire_image";
            case RenderWatchdogStage::BuildUi:
                return "build_ui";
            case RenderWatchdogStage::EndCommandBuffer:
                return "end_command_buffer";
            case RenderWatchdogStage::Submit:
                return "submit";
            case RenderWatchdogStage::Present:
                return "present";
        }
        return "unknown";
    }

    static const std::string& RuntimeDiagLogPath() {
        if (g_runtimeDiagLogPath.empty()) {
            const char* basePath = SDL_GetBasePath();
            if (basePath != nullptr && basePath[0] != '\0') {
                g_runtimeDiagLogPath = std::string(basePath) + "spherical_runtime_diag.log";
            } else {
                g_runtimeDiagLogPath = "spherical_runtime_diag.log";
            }
        }
        return g_runtimeDiagLogPath;
    }

    static void ResetRuntimeDiagLog() {
        const std::lock_guard<std::mutex> lock(g_runtimeDiagMutex);
        std::ofstream file(RuntimeDiagLogPath(), std::ios::trunc);
        if (file) {
            file << "[SPHERICAL][Diag] session begin" << std::endl;
        }
    }

    static void AppendRuntimeDiag(const std::string& message) {
        const std::lock_guard<std::mutex> lock(g_runtimeDiagMutex);
        std::ofstream file(RuntimeDiagLogPath(), std::ios::app);
        if (file) {
            file << message << std::endl;
        }
    }

    static void SetRenderWatchdogStage(RenderWatchdogStage stage) {
        g_renderWatchdogStage.store(static_cast<int>(stage), std::memory_order_relaxed);
        g_renderWatchdogSerial.fetch_add(1, std::memory_order_relaxed);
    }

    static void StartRenderWatchdog() {
        if (g_renderWatchdogRunning.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        AppendRuntimeDiag("[SPHERICAL][Watchdog] started");

        g_renderWatchdogThread = std::thread([]() {
            uint64_t lastSerial = g_renderWatchdogSerial.load(std::memory_order_relaxed);
            auto lastProgressTime = std::chrono::steady_clock::now();
            auto lastHeartbeatTime = std::chrono::steady_clock::now();
            bool reportedCurrentStall = false;

            while (g_renderWatchdogRunning.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));

                const uint64_t serial = g_renderWatchdogSerial.load(std::memory_order_relaxed);
                const RenderWatchdogStage stage = static_cast<RenderWatchdogStage>(
                    g_renderWatchdogStage.load(std::memory_order_relaxed));
                const uint64_t frame = g_renderFrameCounter.load(std::memory_order_relaxed);

                const auto now = std::chrono::steady_clock::now();
                const auto heartbeatMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeatTime).count();
                if (heartbeatMs >= 1000) {
                    std::ostringstream hb;
                    hb << "[SPHERICAL][Heartbeat] frame=" << frame
                       << " stage=" << RenderWatchdogStageName(stage)
                       << " serial=" << serial;
                    AppendRuntimeDiag(hb.str());
                    lastHeartbeatTime = now;
                }

                if (serial != lastSerial) {
                    lastSerial = serial;
                    lastProgressTime = now;
                    reportedCurrentStall = false;
                    continue;
                }

                if (stage == RenderWatchdogStage::Idle) {
                    reportedCurrentStall = false;
                    continue;
                }

                const auto stallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastProgressTime).count();
                if (!reportedCurrentStall && stallMs >= 1500) {
                    std::ostringstream oss;
                    oss << "[SPHERICAL][Watchdog] render stall stage="
                        << RenderWatchdogStageName(stage)
                        << " frame=" << frame
                        << " stalled_ms=" << stallMs;
                    const std::string line = oss.str();
                    std::cerr << line << std::endl;
                    AppendRuntimeDiag(line);
                    reportedCurrentStall = true;
                }
            }
        });
    }

    static void StopRenderWatchdog() {
        if (!g_renderWatchdogRunning.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (g_renderWatchdogThread.joinable()) {
            g_renderWatchdogThread.join();
        }

        SetRenderWatchdogStage(RenderWatchdogStage::Idle);
        AppendRuntimeDiag("[SPHERICAL][Watchdog] stopped");
    }

    static void LogRenderSyncEvent(const char* stage, VkResult result, uint64_t frameId) {
        const auto now = std::chrono::steady_clock::now();
        if (g_lastRenderSyncLogTime.time_since_epoch().count() != 0) {
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastRenderSyncLogTime).count();
            if (elapsedMs < 250) {
                return;
            }
        }

        g_lastRenderSyncLogTime = now;
        std::ostringstream oss;
        oss << "[SPHERICAL][RenderSync] frame=" << frameId
            << " stage=" << stage
            << " result=" << static_cast<int>(result);
        const std::string line = oss.str();
        std::cerr << line << std::endl;
        AppendRuntimeDiag(line);
    }

    static bool IsLeftMouseDown(const nk_context* ctx) {
        return ctx != nullptr && ctx->input.mouse.buttons[NK_BUTTON_LEFT].down != 0;
    }

    static bool IsLeftMouseDownAnywhere(const nk_context* ctx) {
        if (IsLeftMouseDown(ctx)) {
            return true;
        }

        float globalX = 0.0f;
        float globalY = 0.0f;
        const Uint32 globalButtons = SDL_GetGlobalMouseState(&globalX, &globalY);
        return (globalButtons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
    }

    static bool WasLeftMousePressed(const nk_context* ctx) {
        if (ctx == nullptr) {
            return false;
        }

        const nk_mouse_button& button = ctx->input.mouse.buttons[NK_BUTTON_LEFT];
        return button.down != 0 && button.clicked != 0;
    }

    static bool WasRightMousePressed(const nk_context* ctx) {
        if (ctx == nullptr) {
            return false;
        }

        const nk_mouse_button& button = ctx->input.mouse.buttons[NK_BUTTON_RIGHT];
        return button.down != 0 && button.clicked != 0;
    }

    static bool IsMouseInsideRect(const nk_context* ctx, const struct nk_rect& rect) {
        if (ctx == nullptr) {
            return false;
        }

        const float mx = ctx->input.mouse.pos.x;
        const float my = ctx->input.mouse.pos.y;
        return mx >= rect.x && mx <= (rect.x + rect.w) &&
               my >= rect.y && my <= (rect.y + rect.h);
    }

    static bool IsPointInsideRect(float x, float y, const struct nk_rect& rect) {
        return x >= rect.x && x <= (rect.x + rect.w) &&
               y >= rect.y && y <= (rect.y + rect.h);
    }

    static float GetWindowHeaderHeight(const nk_context* ctx, const nk_user_font* font = nullptr) {
        const nk_user_font* headerFont = font;
        if (ctx != nullptr && headerFont == nullptr) {
            headerFont = ctx->style.font;
        }
        if (ctx == nullptr || headerFont == nullptr) {
            return 24.0f;
        }

        // Match Nuklear's actual header draw path:
        //   font height + header padding + label padding + 1px background overlap.
        return std::max(
            24.0f,
            headerFont->height +
                2.0f * ctx->style.window.header.padding.y +
                2.0f * ctx->style.window.header.label_padding.y +
                1.0f
        );
    }

    static int CursorPriority(CursorRequest request) {
        switch (request) {
            case CursorRequest::ResizeNwse:
            case CursorRequest::ResizeNesw:
            case CursorRequest::ResizeNs:
            case CursorRequest::ResizeEw:
                return 3;
            case CursorRequest::Default:
            default:
                return 1;
        }
    }

    static void RequestCursor(CursorRequest request) {
        if (CursorPriority(request) >= CursorPriority(g_cursorState.requested)) {
            g_cursorState.requested = request;
        }
    }

    static SDL_SystemCursor ResolveSystemCursor(CursorRequest request) {
        switch (request) {
            case CursorRequest::ResizeNwse:
                return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
            case CursorRequest::ResizeNesw:
                return SDL_SYSTEM_CURSOR_NESW_RESIZE;
            case CursorRequest::ResizeNs:
                return SDL_SYSTEM_CURSOR_NS_RESIZE;
            case CursorRequest::ResizeEw:
                return SDL_SYSTEM_CURSOR_EW_RESIZE;
            case CursorRequest::Default:
            default:
                return SDL_SYSTEM_CURSOR_DEFAULT;
        }
    }

    static SDL_Cursor* EnsureCursor(CursorRequest request) {
        SDL_Cursor** slot = &g_cursorState.arrow;
        switch (request) {
            case CursorRequest::ResizeNwse:
                slot = &g_cursorState.nwse;
                break;
            case CursorRequest::ResizeNesw:
                slot = &g_cursorState.nesw;
                break;
            case CursorRequest::ResizeNs:
                slot = &g_cursorState.ns;
                break;
            case CursorRequest::ResizeEw:
                slot = &g_cursorState.ew;
                break;
            case CursorRequest::Default:
            default:
                slot = &g_cursorState.arrow;
                break;
        }

        if (*slot == nullptr) {
            *slot = SDL_CreateSystemCursor(ResolveSystemCursor(request));
        }

        return *slot;
    }

    static void ApplyRequestedCursor() {
        if (g_backend.window == nullptr) {
            return;
        }

        if (g_cursorState.requested == g_cursorState.applied) {
            return;
        }

        if (SDL_Cursor* cursor = EnsureCursor(g_cursorState.requested)) {
            SDL_SetCursor(cursor);
            g_cursorState.applied = g_cursorState.requested;
        }
    }

    static void DestroyCursor(SDL_Cursor*& cursor) {
        if (cursor != nullptr) {
            SDL_DestroyCursor(cursor);
            cursor = nullptr;
        }
    }

    static void ShutdownCursors() {
        DestroyCursor(g_cursorState.arrow);
        DestroyCursor(g_cursorState.nwse);
        DestroyCursor(g_cursorState.nesw);
        DestroyCursor(g_cursorState.ns);
        DestroyCursor(g_cursorState.ew);
        g_cursorState.requested = CursorRequest::Default;
        g_cursorState.applied = CursorRequest::Default;
    }

    static void DrawCenterMarkerOverlay(nk_context* context, const VkExtent2D& framebufferExtent) {
        if (context == nullptr) {
            return;
        }

        const float width = static_cast<float>(framebufferExtent.width);
        const float height = static_cast<float>(framebufferExtent.height);
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }

        const nk_flags flags = NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT | NK_WINDOW_BACKGROUND;
        if (nk_begin(context, "__SPHERICAL_CENTER_MARKER_OVERLAY", nk_rect(0.0f, 0.0f, width, height), flags)) {
            if (context->current != nullptr) {
                nk_command_buffer* buffer = &context->current->buffer;
                const float cx = width * 0.5f;
                const float cy = height * 0.5f;
                const nk_color red = nk_rgb(230, 60, 60);
                const float arm = 6.0f;

                nk_stroke_line(buffer, cx - arm, cy, cx + arm, cy, 1.5f, red);
                nk_stroke_line(buffer, cx, cy - arm, cx, cy + arm, 1.5f, red);
                nk_fill_circle(buffer, nk_rect(cx - 1.5f, cy - 1.5f, 3.0f, 3.0f), red);
            }
        }
        nk_end(context);
    }

    static void DrawWorkspaceDockPreviewOverlay(nk_context* context, const VkExtent2D& framebufferExtent) {
        if (context == nullptr || !g_dropTargetState.active) {
            return;
        }

        const auto containerIt = g_workspaceContainerStates.find(g_dropTargetState.containerTitle);
        if (containerIt == g_workspaceContainerStates.end() || !containerIt->second.initialized) {
            return;
        }

        const float width = static_cast<float>(framebufferExtent.width);
        const float height = static_cast<float>(framebufferExtent.height);
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }

        const float centerX = width * 0.5f;
        const float centerY = height * 0.5f;
        const WorkspaceContainerState& containerState = containerIt->second;
        const float headerHeight = containerState.headerHeight > 0.0f
            ? containerState.headerHeight
            : GetWindowHeaderHeight(context);

        struct nk_rect containerBounds = nk_rect(
            centerX + containerState.offsetFromCenterX,
            centerY + containerState.offsetFromCenterY,
            containerState.width,
            containerState.height
        );
        struct nk_rect bodyRect = containerBounds;
        bodyRect.y += headerHeight;
        bodyRect.h = std::max(0.0f, bodyRect.h - headerHeight);

        const nk_color previousBackground = context->style.window.background;
        const nk_style_item previousFixedBackground = context->style.window.fixed_background;
        const nk_color transparent = nk_rgba(0, 0, 0, 0);
        context->style.window.background = transparent;
        context->style.window.fixed_background = nk_style_item_color(transparent);

        const nk_flags flags = NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT;
        if (nk_begin(context, "__SPHERICAL_WORKSPACE_DOCK_PREVIEW_OVERLAY", nk_rect(0.0f, 0.0f, width, height), flags)) {
            if (context->current != nullptr) {
                nk_command_buffer* buffer = &context->current->buffer;
                nk_push_scissor(buffer, nk_rect(0.0f, 0.0f, width, height));

                if (g_dropTargetState.hoveredLeaf == nullptr) {
                    if (bodyRect.w > 0.0f && bodyRect.h > 0.0f) {
                        nk_fill_rect(buffer, bodyRect, 0.0f, nk_rgba(100, 150, 220, 55));
                        nk_stroke_rect(buffer, bodyRect, 0.0f, 2.0f, nk_rgb(120, 180, 255));
                    }
                } else {
                    const struct nk_rect& leafRect = g_dropTargetState.hoveredLeaf->computedRect;
                    const nk_color zoneActive = nk_rgba(100, 150, 200, 100);
                    const nk_color zoneDimmed = nk_rgba(100, 150, 200, 40);
                    const nk_color zoneBorder = nk_rgb(100, 150, 200);
                    const float w = leafRect.w;
                    const float h = leafRect.h;
                    const float marginW = w * 0.25f;
                    const float marginH = h * 0.25f;

                    const struct nk_rect centerRect = nk_rect(
                        leafRect.x + marginW,
                        leafRect.y + marginH,
                        w * 0.5f,
                        h * 0.5f
                    );

                    auto drawPolygonZone = [&](DropTargetState::DropZone zone, const float* points, int pointCount) {
                        const nk_color color = (g_dropTargetState.zone == zone) ? zoneActive : zoneDimmed;
                        nk_fill_polygon(buffer, points, pointCount, color);
                        nk_stroke_polygon(buffer, points, pointCount, 1.0f, zoneBorder);
                    };

                    auto drawRectZone = [&](DropTargetState::DropZone zone, const struct nk_rect& rect) {
                        const nk_color color = (g_dropTargetState.zone == zone) ? zoneActive : zoneDimmed;
                        nk_fill_rect(buffer, rect, 0.0f, color);
                        nk_stroke_rect(buffer, rect, 0.0f, 1.0f, zoneBorder);
                    };

                    const float topZonePoints[] = {
                        leafRect.x, leafRect.y,
                        leafRect.x + leafRect.w, leafRect.y,
                        centerRect.x + centerRect.w, centerRect.y,
                        centerRect.x, centerRect.y
                    };
                    const float leftZonePoints[] = {
                        leafRect.x, leafRect.y,
                        centerRect.x, centerRect.y,
                        centerRect.x, centerRect.y + centerRect.h,
                        leafRect.x, leafRect.y + leafRect.h
                    };
                    const float bottomZonePoints[] = {
                        centerRect.x, centerRect.y + centerRect.h,
                        centerRect.x + centerRect.w, centerRect.y + centerRect.h,
                        leafRect.x + leafRect.w, leafRect.y + leafRect.h,
                        leafRect.x, leafRect.y + leafRect.h
                    };
                    const float rightZonePoints[] = {
                        centerRect.x + centerRect.w, centerRect.y,
                        leafRect.x + leafRect.w, leafRect.y,
                        leafRect.x + leafRect.w, leafRect.y + leafRect.h,
                        centerRect.x + centerRect.w, centerRect.y + centerRect.h
                    };

                    drawPolygonZone(DropTargetState::DropZone::Top, topZonePoints, 4);
                    drawPolygonZone(DropTargetState::DropZone::Left, leftZonePoints, 4);
                    drawPolygonZone(DropTargetState::DropZone::Bottom, bottomZonePoints, 4);
                    drawPolygonZone(DropTargetState::DropZone::Right, rightZonePoints, 4);
                    drawRectZone(DropTargetState::DropZone::Center, centerRect);
                }
            }
        }
        nk_end(context);
        context->style.window.background = previousBackground;
        context->style.window.fixed_background = previousFixedBackground;
    }

    static void DrawWorkspaceSplitterOverlay(nk_context* context, const VkExtent2D& framebufferExtent) {
        Spherical::DockOverlayOps::DrawWorkspaceSplitterOverlay(
            context,
            framebufferExtent,
            g_workspaceContainerStates,
            g_splitterDrag);
    }

    // ===== BSP Tree Utility Functions =====

    using DockMinSize = Spherical::DockLayoutOps::DockMinSize;

    static struct nk_rect InsetDockedPanelRect(const struct nk_rect& rect) {
        return Spherical::DockTreePrimitives::InsetDockedPanelRect(rect);
    }

    static float GetDockNodeMainAxisExtent(const DockNode* node, Spherical::DockLayout splitDirection) {
        return Spherical::DockTreePrimitives::GetDockNodeMainAxisExtent(node, splitDirection);
    }

    static float GetDockMinSizeMainAxisExtent(const DockMinSize& minSize, Spherical::DockLayout splitDirection) {
        return (splitDirection == Spherical::DockLayout::SideBySide) ? minSize.width : minSize.height;
    }

    static float SumDockExtents(const std::vector<float>& extents) {
        return Spherical::DockTreePrimitives::SumDockExtents(extents);
    }

    static void EnsureDockSplitStorage(DockNode* node) {
        Spherical::DockTreePrimitives::EnsureDockSplitStorage(node);
    }

    static void RefreshDockGroupLegacyRatios(DockNode* node) {
        Spherical::DockTreePrimitives::RefreshDockGroupLegacyRatios(node);
    }

    static std::unique_ptr<DockNode> BuildDockGroupNode(Spherical::DockLayout splitDirection,
                                                        std::vector<std::unique_ptr<DockNode>> children,
                                                        std::vector<float> preferredChildExtents) {
        return Spherical::DockTreePrimitives::BuildDockGroupNode(
            splitDirection,
            std::move(children),
            std::move(preferredChildExtents));
    }

    static void FlattenSameAxisChildGroups(DockNode* node) {
        Spherical::DockTreePrimitives::FlattenSameAxisChildGroups(node);
    }

    static bool IsDockTerminalNode(const DockNode* node) {
        return Spherical::DockTreePrimitives::IsDockTerminalNode(node);
    }

    int CountDockNodeLeaves(const DockNode* node) {
        return Spherical::DockTreePrimitives::CountDockNodeLeaves(node);
    }

    DockNode* FindDockNodeByPanelTitle(DockNode* node, const std::string& panelTitle) {
        return Spherical::DockTreePrimitives::FindDockNodeByPanelTitle(node, panelTitle);
    }

    DockMinSize ComputeDockNodeMinimumSize(const DockNode* node, float leafMinWidth, float leafMinHeight) {
        return Spherical::DockLayoutOps::ComputeDockNodeMinimumSize(node, leafMinWidth, leafMinHeight);
    }

    DockMinSize RebalanceDockNodeSplitRatios(DockNode* node, float leafMinWidth, float leafMinHeight) {
        return Spherical::DockLayoutOps::RebalanceDockNodeSplitRatios(node, leafMinWidth, leafMinHeight);
    }

    static struct nk_rect GetDockSplitterLineRect(const DockNode* node, std::size_t boundaryIndex, float thickness) {
        return Spherical::DockSplitterOps::GetDockSplitterLineRect(node, boundaryIndex, thickness);
    }

    static struct nk_rect GetDockSplitterHitRect(const DockNode* node, std::size_t boundaryIndex, float thickness) {
        return Spherical::DockSplitterOps::GetDockSplitterHitRect(node, boundaryIndex, thickness);
    }

    static bool DockTreeContainsNode(const DockNode* root, const DockNode* target) {
        return Spherical::DockSplitterOps::DockTreeContainsNode(root, target);
    }

    static bool IsValidDockSplitterRef(const DockNode* root, const DockSplitterRef& ref) {
        return Spherical::DockSplitterOps::IsValidDockSplitterRef(root, ref);
    }

    static bool DockSplitterRefsEqual(const DockSplitterRef& a, const DockSplitterRef& b) {
        return Spherical::DockSplitterOps::DockSplitterRefsEqual(a, b);
    }

    static void PruneWorkspaceSplitterLinks(WorkspaceContainerState& containerState) {
        Spherical::DockSplitterOps::PruneWorkspaceSplitterLinks(containerState);
    }

    static void PruneWorkspaceSplitterIntersections(WorkspaceContainerState& containerState) {
        Spherical::DockSplitterOps::PruneWorkspaceSplitterIntersections(containerState);
    }

    static void CollectDockSplitterSegmentGeometry(const DockNode* node,
                                                  Spherical::DockLayout splitDirection,
                                                  std::vector<DockSplitterSegmentGeometry>& outSegments) {
        Spherical::DockSplitterOps::CollectDockSplitterSegmentGeometry(node, splitDirection, outSegments);
    }

    static std::vector<DockSplitterRef> CollectDockSplitterChain(const DockNode* root,
                                                                 Spherical::DockLayout splitDirection,
                                                                 const std::vector<DockSplitterRef>& seedRefs) {
        return Spherical::DockSplitterOps::CollectDockSplitterChain(root, splitDirection, seedRefs);
    }

    static std::vector<DockSplitterRef> GetLinkedDockSplitterRefs(WorkspaceContainerState& containerState,
                                                                  DockNode* node,
                                                                  std::size_t boundaryIndex) {
        return Spherical::DockSplitterOps::GetLinkedDockSplitterRefs(containerState, node, boundaryIndex);
    }

    static bool AreDockSplitterRefsLogicallyUnified(WorkspaceContainerState& containerState,
                                                    Spherical::DockLayout splitDirection,
                                                    const DockSplitterRef& first,
                                                    const DockSplitterRef& second) {
        return Spherical::DockSplitterOps::AreDockSplitterRefsLogicallyUnified(containerState, splitDirection, first, second);
    }

    static void RegisterLinkedSplitterGroup(WorkspaceContainerState& containerState,
                                            Spherical::DockLayout splitDirection,
                                            std::vector<DockSplitterRef> members) {
        Spherical::DockSplitterOps::RegisterLinkedSplitterGroup(containerState, splitDirection, std::move(members));
    }

    static void RegisterSplitterIntersection(WorkspaceContainerState& containerState,
                                            float x,
                                            float y,
                                            std::vector<DockSplitterRef> verticalMembers,
                                            std::vector<DockSplitterRef> horizontalMembers) {
        Spherical::DockSplitterOps::RegisterSplitterIntersection(
            containerState,
            x,
            y,
            std::move(verticalMembers),
            std::move(horizontalMembers));
    }

    static DockSplitterHit FindDockSplitterAtPoint(DockNode* node, float x, float y,
                                                   float visualThickness, float hitThickness) {
        return Spherical::DockSplitterOps::FindDockSplitterAtPoint(node, x, y, visualThickness, hitThickness);
    }

    static bool GetDockSplitterClampRange(const DockNode* node,
                                          std::size_t boundaryIndex,
                                          float leafMinWidth,
                                          float leafMinHeight,
                                          float& outMinPosition,
                                          float& outMaxPosition) {
        return Spherical::DockLayoutOps::GetDockSplitterClampRange(
            node,
            boundaryIndex,
            leafMinWidth,
            leafMinHeight,
            outMinPosition,
            outMaxPosition);
    }

    static float ClampDockSplitterPosition(const DockNode* node, std::size_t boundaryIndex, float proposedPosition,
                                           float leafMinWidth, float leafMinHeight) {
        return Spherical::DockLayoutOps::ClampDockSplitterPosition(
            node,
            boundaryIndex,
            proposedPosition,
            leafMinWidth,
            leafMinHeight);
    }

    static float ClampLinkedDockSplitterPosition(WorkspaceContainerState& containerState,
                                                 DockNode* node,
                                                 std::size_t boundaryIndex,
                                                 float proposedPosition,
                                                 float leafMinWidth,
                                                 float leafMinHeight) {
        return Spherical::DockLayoutOps::ClampLinkedDockSplitterPosition(
            containerState,
            node,
            boundaryIndex,
            proposedPosition,
            leafMinWidth,
            leafMinHeight);
    }

    static void AdjustDockSplitterToPosition(DockNode* node, std::size_t boundaryIndex, float proposedPosition,
                                             float leafMinWidth, float leafMinHeight) {
        Spherical::DockLayoutOps::AdjustDockSplitterToPosition(
            node,
            boundaryIndex,
            proposedPosition,
            leafMinWidth,
            leafMinHeight);
    }

    static float GetWorkspaceSplitterFlashAlpha(const WorkspaceContainerState& containerState,
                                                const DockNode* root,
                                                const DockNode* splitNode,
                                                std::size_t boundaryIndex) {
        return Spherical::DockOverlayOps::GetWorkspaceSplitterFlashAlpha(
            containerState,
            root,
            splitNode,
            boundaryIndex);
    }

    static void NormalizeDockNodeLayoutForBounds(DockNode* node, const struct nk_rect& parentRect,
                                                 float leafMinWidth, float leafMinHeight) {
        Spherical::DockLayoutOps::NormalizeDockNodeLayoutForBounds(node, parentRect, leafMinWidth, leafMinHeight);
    }

    static void DrawDockSplitLinesForClipRect(nk_command_buffer* buffer, const DockNode* root, const struct nk_rect& clipRect,
                                              const DockSplitterHit& activeHit, const DockSplitterHit& hoveredHit) {
        Spherical::DockLayoutOps::DrawDockSplitLinesForClipRect(buffer, root, clipRect, activeHit, hoveredHit);
    }

    void ComputeDockNodeRects(DockNode* node, const struct nk_rect& parentRect) {
        Spherical::DockLayoutOps::ComputeDockNodeRects(node, parentRect);
    }

    DockNode* FindLeafAtPoint(DockNode* node, float x, float y) {
        return Spherical::DockSplitterOps::FindLeafAtPoint(node, x, y);
    }

    DropTargetState::DropZone DetermineDropZone(const struct nk_rect& leafRect, float x, float y) {
        return Spherical::DockSplitterOps::DetermineDropZone(leafRect, x, y);
    }

    static bool TryUnifyDockSplitterBoundary(std::unique_ptr<DockNode>& root,
                                             WorkspaceContainerState& containerState,
                                             DockNode* activeNode,
                                             std::size_t activeBoundaryIndex,
                                             float snapThreshold,
                                             float leafMinWidth,
                                             float leafMinHeight,
                                             DockSplitterHit& outUnifiedHit) {
        return Spherical::DockReconcileOps::TryUnifyDockSplitterBoundary(
            root,
            containerState,
            activeNode,
            activeBoundaryIndex,
            snapThreshold,
            leafMinWidth,
            leafMinHeight,
            outUnifiedHit);
    }

    static bool CascadeWorkspaceContainerSplitterUnifications(std::unique_ptr<DockNode>& root,
                                                              WorkspaceContainerState& containerState,
                                                              const struct nk_rect& rootRect,
                                                              float snapThreshold,
                                                              float leafMinWidth,
                                                              float leafMinHeight) {
        return Spherical::DockReconcileOps::CascadeWorkspaceContainerSplitterUnifications(
            root,
            containerState,
            rootRect,
            snapThreshold,
            leafMinWidth,
            leafMinHeight);
    }

    static void ReconcileWorkspaceContainerSplitters(std::unique_ptr<DockNode>& root,
                                                     WorkspaceContainerState& containerState,
                                                     const struct nk_rect& rootRect,
                                                     float leafMinWidth,
                                                     float leafMinHeight) {
        Spherical::DockReconcileOps::ReconcileWorkspaceContainerSplitters(
            root,
            containerState,
            rootRect,
            leafMinWidth,
            leafMinHeight);
    }

    void CollapseDockGroups(std::unique_ptr<DockNode>& node) {
        Spherical::DockReconcileOps::CollapseDockGroups(node);
    }

    void RemoveDockNodeAndReflow(std::unique_ptr<DockNode>& root, WorkspaceContainerState* containerState, DockNode* nodeToRemove) {
        Spherical::DockMutationOps::RemoveDockNodeAndReflow(root, containerState, nodeToRemove, &g_splitterDrag);
    }

    void InsertDockNode(std::unique_ptr<DockNode>& root,
                        WorkspaceContainerState* containerState,
                        DockNode* targetLeaf,
                        std::string newPanelTitle,
                        DropTargetState::DropZone zone) {
        Spherical::DockMutationOps::InsertDockNode(
            root,
            containerState,
            targetLeaf,
            std::move(newPanelTitle),
            zone,
            &g_splitterDrag);
    }

    class UIPainterImpl : public Spherical::UIPainter {
    private:
        struct PanelSubsectionFrameState {
            std::string hierarchyPath;
            bool expanded = true;
            bool parentVisible = true;
        };

        struct PanelBodyStyleSnapshot {
            nk_color background;
            nk_style_item fixedBackground;
        };

        struct StyleItemStateSnapshot {
            nk_style_item normal;
            nk_style_item hover;
            nk_style_item active;
        };

        struct ColorStateSnapshot {
            nk_color normal;
            nk_color hover;
            nk_color active;
        };

        struct TextInputTextColorSnapshot {
            nk_color cursorNormal;
            nk_color cursorHover;
            nk_color cursorTextNormal;
            nk_color cursorTextHover;
            nk_color textNormal;
            nk_color textHover;
            nk_color textActive;
            nk_color selectedTextNormal;
            nk_color selectedTextHover;
        };

        nk_context* m_ctx = nullptr;
        VkExtent2D m_framebufferExtent{};
        const char* m_activePanelTitle = nullptr;
        Spherical::UIRect m_currentPanelBounds{};
        Spherical::UIRect m_currentPanelContentBounds{};
        std::vector<PanelSubsectionFrameState> m_panelSubsectionStack;
        std::vector<nk_color> m_textColorStack;
        std::vector<PanelBodyStyleSnapshot> m_panelBodyColorStack;
        std::vector<StyleItemStateSnapshot> m_panelTitleBarColorStack;
        std::vector<nk_color> m_panelBorderColorStack;
        std::vector<ColorStateSnapshot> m_panelTitleTextColorStack;
        std::vector<ColorStateSnapshot> m_radioButtonTextColorStack;
        std::vector<StyleItemStateSnapshot> m_buttonBackgroundColorStack;
        std::vector<nk_style_item> m_buttonHoverBackgroundColorStack;
        std::vector<nk_style_item> m_buttonClickedBackgroundColorStack;
        std::vector<float> m_buttonHeightStack;
        std::vector<float> m_buttonWidthStack;
        std::vector<float> m_buttonCornerRadiusStack;
        std::vector<float> m_buttonBorderThicknessStack;
        std::vector<struct nk_vec2> m_buttonPaddingStack;
        std::vector<Spherical::ButtonStyle> m_buttonStyleStack;
        std::vector<nk_color> m_buttonHighlightColorStack;
        std::vector<nk_color> m_buttonShadowColorStack;
        std::vector<nk_color> m_buttonBorderColorStack;
        std::vector<ColorStateSnapshot> m_buttonTextColorStack;
        std::vector<StyleItemStateSnapshot> m_textInputBackgroundColorStack;
        std::vector<TextInputTextColorSnapshot> m_textInputTextColorStack;

        float current_font_height() const {
            if (m_ctx != nullptr && m_ctx->style.font != nullptr) {
                return m_ctx->style.font->height;
            }

            if (nk_user_font* defaultFont = Spherical::FontRenderer::GetFontHandle(Spherical::FontStyle::Regular)) {
                return defaultFont->height;
            }

            return 12.0f;
        }

        float label_row_height() const {
            return std::ceil(current_font_height() * 1.8f);
        }

        float control_row_height() const {
            return std::ceil(current_font_height() * 2.0f);
        }

        float subsection_header_row_height() const {
            return std::max(control_row_height(), std::ceil(current_font_height() * 1.9f));
        }

        float subsection_indent_step() const {
            return std::max(12.0f, std::ceil(current_font_height() * 1.1f));
        }

        float button_row_height() const {
            const struct nk_vec2 padding = current_button_padding();
            const float border = current_button_border_thickness();
            const float requestedHeight = m_buttonHeightStack.empty()
                ? std::ceil(current_font_height() * 2.25f)
                : m_buttonHeightStack.back();
            const float minHeight = std::ceil(current_font_height() + 2.0f * padding.y + 2.0f * border + 2.0f);
            return std::max(requestedHeight, minHeight);
        }

        float spacing_row_height() const {
            return std::max(4.0f, std::ceil(current_font_height() * 0.8f));
        }
        
        //scrollbar drag helpers
        void release_scrollbar_drag_if_needed(const char* title) {
            if (title == nullptr) {
                return;
            }

            if (g_scrollbarDrag.active && g_scrollbarDrag.windowTitle == title) {
                g_scrollbarDrag = {};
            }
        }

        void apply_active_vertical_scrollbar_drag(const char* title) {
            if (m_ctx == nullptr || title == nullptr) {
                return;
            }

            if (!g_scrollbarDrag.active || g_scrollbarDrag.windowTitle != title) {
                return;
            }

            if (!IsLeftMouseDown(m_ctx)) {
                release_scrollbar_drag_if_needed(title);
                return;
            }

            if (g_scrollbarDrag.trackH <= 0.0f || g_scrollbarDrag.thumbH <= 0.0f || g_scrollbarDrag.maxScrollY <= 0.0f) {
                return;
            }

            nk_uint scrollX = 0;
            nk_uint scrollY = 0;
            nk_window_get_scroll(m_ctx, &scrollX, &scrollY);

            const float dragTravel = std::max(1.0f, g_scrollbarDrag.trackH - g_scrollbarDrag.thumbH);
            float desiredThumbY = m_ctx->input.mouse.pos.y - g_scrollbarDrag.grabOffsetY;
            desiredThumbY = std::clamp(desiredThumbY, g_scrollbarDrag.trackY, g_scrollbarDrag.trackY + dragTravel);

            const float normalizedThumb = (desiredThumbY - g_scrollbarDrag.trackY) / dragTravel;
            const float desiredScrollY = std::clamp(normalizedThumb * g_scrollbarDrag.maxScrollY, 0.0f, g_scrollbarDrag.maxScrollY);

            nk_window_set_scroll(m_ctx, scrollX, static_cast<nk_uint>(desiredScrollY + 0.5f));
            m_ctx->input.mouse.scroll_delta.y = 0.0f;
        }

        void refresh_vertical_scrollbar_drag_state(const char* title) {
            if (m_ctx == nullptr || title == nullptr) {
                return;
            }

            nk_panel* panel = nk_window_get_panel(m_ctx);
            if (panel == nullptr) {
                return;
            }

            nk_uint scrollX = 0;
            nk_uint scrollY = 0;
            nk_window_get_scroll(m_ctx, &scrollX, &scrollY);

            const struct nk_rect windowBounds = nk_window_get_bounds(m_ctx);
            const struct nk_rect contentRegion = nk_window_get_content_region(m_ctx);

            const float visibleContentHeight = contentRegion.h;
            // nk_end() advances panel->at_y by panel->row.height; include it here since we run before nk_end().
            const float estimatedContentBottomY = panel->at_y + panel->row.height;
            const float totalContentHeight = std::max(estimatedContentBottomY - contentRegion.y, visibleContentHeight);
            const float maxScrollY = std::max(0.0f, totalContentHeight - visibleContentHeight);

            if (maxScrollY <= 0.0f) {
                release_scrollbar_drag_if_needed(title);
                return;
            }

            const float scrollbarWidth = m_ctx->style.window.scrollbar_size.x;
            if (scrollbarWidth <= 0.0f) {
                release_scrollbar_drag_if_needed(title);
                return;
            }

            struct nk_rect track{};
            track.x = windowBounds.x + windowBounds.w - scrollbarWidth;
            track.y = contentRegion.y;
            track.w = scrollbarWidth;
            track.h = visibleContentHeight;

            // Expand hit area to include the left-edge border/padding around the visual scrollbar.
            const float edgeExpand = std::max(
                2.0f,
                m_ctx->style.window.border + m_ctx->style.scrollv.border + m_ctx->style.scrollv.padding.x
            );

            struct nk_rect trackHit = track;
            trackHit.x -= edgeExpand;
            trackHit.w += edgeExpand;

            if (track.h <= 0.0f) {
                release_scrollbar_drag_if_needed(title);
                return;
            }

            const float thumbRatio = visibleContentHeight / totalContentHeight;
            const float thumbH = std::max(16.0f, track.h * thumbRatio);
            const float thumbTravel = std::max(1.0f, track.h - thumbH);
            const float normalizedScroll = (maxScrollY > 0.0f) ? (static_cast<float>(scrollY) / maxScrollY) : 0.0f;

            struct nk_rect thumb{};
            thumb.x = track.x;
            thumb.y = track.y + normalizedScroll * thumbTravel;
            thumb.w = track.w;
            thumb.h = thumbH;

            const bool leftPressed = WasLeftMousePressed(m_ctx);
            const bool leftDown = IsLeftMouseDown(m_ctx);

            if (!leftDown) {
                release_scrollbar_drag_if_needed(title);
                return;
            }

            if (!g_scrollbarDrag.active && leftPressed) {
                const bool clickedThumb = IsMouseInsideRect(m_ctx, thumb);
                const bool clickedTrack = IsMouseInsideRect(m_ctx, trackHit);

                if (clickedThumb || clickedTrack) {
                    g_scrollbarDrag.active = true;
                    g_scrollbarDrag.windowTitle = title;
                    g_scrollbarDrag.dragStartMouseY = m_ctx->input.mouse.pos.y;
                    g_scrollbarDrag.dragStartScrollY = static_cast<float>(scrollY);
                    g_scrollbarDrag.trackY = track.y;
                    g_scrollbarDrag.trackH = track.h;
                    g_scrollbarDrag.thumbH = thumb.h;
                    g_scrollbarDrag.maxScrollY = maxScrollY;

                    if (clickedThumb) {
                        // Preserve exact grab position inside thumb for 1:1 dragging.
                        g_scrollbarDrag.grabOffsetY = m_ctx->input.mouse.pos.y - thumb.y;
                    } else {
                        // Track click: snap thumb center to cursor, then continue drag with same math.
                        g_scrollbarDrag.grabOffsetY = g_scrollbarDrag.thumbH * 0.5f;
                    }

                    // Prevent Nuklear's built-in track-click paging from overriding custom behavior.
                    m_ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked = 0;
                }
            }

            if (g_scrollbarDrag.active && g_scrollbarDrag.windowTitle == title) {
                g_scrollbarDrag.trackY = track.y;
                g_scrollbarDrag.trackH = track.h;
                g_scrollbarDrag.thumbH = thumb.h;
                g_scrollbarDrag.maxScrollY = maxScrollY;
                m_ctx->input.mouse.scroll_delta.y = 0.0f;
            }
        }
        
        static nk_color ToNkColor(const Spherical::UIColor& c) {
            auto toByte = [](float v) -> nk_byte {
                return static_cast<nk_byte>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            return nk_rgba(toByte(c.r), toByte(c.g), toByte(c.b), toByte(c.a));
        }

        static nk_style_item ToNkStyleItem(const Spherical::UIColor& c) {
            return nk_style_item_color(ToNkColor(c));
        }

        static struct nk_vec2 ToNkVec2(const Spherical::UIVec2& v) {
            return nk_vec2(v.x, v.y);
        }

        static nk_color BlendColor(const nk_color& from, const nk_color& to, float t) {
            const float clampedT = std::clamp(t, 0.0f, 1.0f);
            const auto blendChannel = [clampedT](nk_byte a, nk_byte b) -> nk_byte {
                const float blended = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * clampedT;
                return static_cast<nk_byte>(std::clamp(blended, 0.0f, 255.0f) + 0.5f);
            };
            return nk_rgba(
                blendChannel(from.r, to.r),
                blendChannel(from.g, to.g),
                blendChannel(from.b, to.b),
                blendChannel(from.a, to.a));
        }

        static nk_color LightenColor(const nk_color& color, float amount) {
            return BlendColor(color, nk_rgb(255, 255, 255), amount);
        }

        static nk_color DarkenColor(const nk_color& color, float amount) {
            return BlendColor(color, nk_rgb(0, 0, 0), amount);
        }

        static nk_color GetStyleItemColor(const nk_style_item& item, const nk_color& fallback) {
            if (item.type == NK_STYLE_ITEM_COLOR) {
                return item.data.color;
            }
            return fallback;
        }

        float current_button_corner_radius() const {
            if (!m_buttonCornerRadiusStack.empty()) {
                return m_buttonCornerRadiusStack.back();
            }
            return (m_ctx != nullptr) ? m_ctx->style.button.rounding : 0.0f;
        }

        float current_button_width(float availableWidth) const {
            if (m_buttonWidthStack.empty()) {
                return availableWidth;
            }

            const float requestedWidth = m_buttonWidthStack.back();
            if (requestedWidth <= 0.0f) {
                return availableWidth;
            }

            return std::min(requestedWidth, availableWidth);
        }

        float current_button_border_thickness() const {
            if (!m_buttonBorderThicknessStack.empty()) {
                return m_buttonBorderThicknessStack.back();
            }
            return (m_ctx != nullptr) ? m_ctx->style.button.border : 0.0f;
        }

        struct nk_vec2 current_button_padding() const {
            if (!m_buttonPaddingStack.empty()) {
                return m_buttonPaddingStack.back();
            }
            return (m_ctx != nullptr) ? m_ctx->style.button.padding : nk_vec2(10.0f, 6.0f);
        }

        Spherical::ButtonStyle current_button_style() const {
            if (!m_buttonStyleStack.empty()) {
                return m_buttonStyleStack.back();
            }
            return Spherical::ButtonStyle::Flat;
        }

        nk_color current_button_highlight_color(const nk_color& baseColor) const {
            if (!m_buttonHighlightColorStack.empty()) {
                return m_buttonHighlightColorStack.back();
            }
            return LightenColor(baseColor, 0.55f);
        }

        nk_color current_button_shadow_color(const nk_color& baseColor) const {
            if (!m_buttonShadowColorStack.empty()) {
                return m_buttonShadowColorStack.back();
            }
            return DarkenColor(baseColor, 0.45f);
        }

        void draw_button_bevel(nk_command_buffer* buffer, const struct nk_rect& bounds, float borderThickness,
                               const nk_color& baseColor, bool pressed) const {
            if (buffer == nullptr || bounds.w <= 2.0f || bounds.h <= 2.0f) {
                return;
            }

            const float inset = std::max(1.0f, borderThickness);
            const float left = bounds.x + inset;
            const float top = bounds.y + inset;
            const float right = bounds.x + bounds.w - inset - 1.0f;
            const float bottom = bounds.y + bounds.h - inset - 1.0f;
            if (right <= left || bottom <= top) {
                return;
            }

            nk_color topLeft = current_button_highlight_color(baseColor);
            nk_color bottomRight = current_button_shadow_color(baseColor);
            if (pressed) {
                std::swap(topLeft, bottomRight);
            }

            nk_stroke_line(buffer, left, top, right, top, 1.0f, topLeft);
            nk_stroke_line(buffer, left, top, left, bottom, 1.0f, topLeft);
            nk_stroke_line(buffer, left, bottom, right, bottom, 1.0f, bottomRight);
            nk_stroke_line(buffer, right, top, right, bottom, 1.0f, bottomRight);

            if ((right - left) >= 4.0f && (bottom - top) >= 4.0f) {
                const float innerLeft = left + 1.0f;
                const float innerTop = top + 1.0f;
                const float innerRight = right - 1.0f;
                const float innerBottom = bottom - 1.0f;
                const nk_color softTopLeft = BlendColor(topLeft, baseColor, 0.45f);
                const nk_color softBottomRight = BlendColor(bottomRight, baseColor, 0.45f);
                nk_stroke_line(buffer, innerLeft, innerTop, innerRight, innerTop, 1.0f, softTopLeft);
                nk_stroke_line(buffer, innerLeft, innerTop, innerLeft, innerBottom, 1.0f, softTopLeft);
                nk_stroke_line(buffer, innerLeft, innerBottom, innerRight, innerBottom, 1.0f, softBottomRight);
                nk_stroke_line(buffer, innerRight, innerTop, innerRight, innerBottom, 1.0f, softBottomRight);
            }
        }

        bool draw_custom_button(const char* label) {
            if (m_ctx == nullptr || m_ctx->current == nullptr || m_ctx->current->layout == nullptr) {
                return false;
            }

            nk_window* win = m_ctx->current;
            nk_panel* layout = win->layout;
            const nk_style_button* style = &m_ctx->style.button;

            struct nk_rect bounds{};
            const nk_widget_layout_states widgetState = nk_widget(&bounds, m_ctx);
            if (!widgetState) {
                return false;
            }

            const float centeredWidth = current_button_width(bounds.w);
            if (centeredWidth < bounds.w) {
                bounds.x += std::max(0.0f, (bounds.w - centeredWidth) * 0.5f);
                bounds.w = centeredWidth;
            }

            const bool isReadOnly = (widgetState == NK_WIDGET_DISABLED) || ((layout->flags & NK_WINDOW_ROM) != 0);
            const bool hovered = !isReadOnly && IsMouseInsideRect(m_ctx, bounds);
            const bool pressed = hovered && IsLeftMouseDown(m_ctx);
            const bool activated = hovered && WasLeftMousePressed(m_ctx);

            const nk_style_item& backgroundItem = pressed ? style->active : (hovered ? style->hover : style->normal);
            const nk_color fallbackBackground = pressed ? m_ctx->style.button.text_background : m_ctx->style.window.background;
            const nk_color backgroundColor = GetStyleItemColor(backgroundItem, fallbackBackground);
            const float borderThickness = std::max(0.0f, current_button_border_thickness());
            const float cornerRadius = std::max(0.0f, current_button_corner_radius());
            const struct nk_vec2 padding = current_button_padding();

            nk_fill_rect(&win->buffer, bounds, cornerRadius, backgroundColor);

            if (current_button_style() == Spherical::ButtonStyle::Embossed) {
                draw_button_bevel(&win->buffer, bounds, borderThickness, backgroundColor, pressed);
            }

            if (borderThickness > 0.0f) {
                nk_stroke_rect(&win->buffer, bounds, cornerRadius, borderThickness, style->border_color);
            }

            const nk_color labelColor = pressed ? style->text_active : (hovered ? style->text_hover : style->text_normal);
            const nk_user_font* buttonFont = m_ctx->style.font;
            if (buttonFont == nullptr) {
                buttonFont = Spherical::FontRenderer::GetFontHandle(Spherical::FontStyle::Regular);
            }
            if (buttonFont != nullptr && label != nullptr) {
                const int labelLength = static_cast<int>(std::strlen(label));
                const float contentX = bounds.x + borderThickness + padding.x;
                const float contentY = bounds.y + borderThickness + padding.y;
                const float contentW = std::max(1.0f, bounds.w - 2.0f * (borderThickness + padding.x));
                const float contentH = std::max(1.0f, bounds.h - 2.0f * (borderThickness + padding.y));
                float textX = contentX;
                const float textWidth = buttonFont->width(buttonFont->userdata, buttonFont->height, label, labelLength);
                if (style->text_alignment & NK_TEXT_ALIGN_CENTERED) {
                    textX = contentX + std::max(0.0f, (contentW - textWidth) * 0.5f);
                } else if (style->text_alignment & NK_TEXT_ALIGN_RIGHT) {
                    textX = std::max(contentX, contentX + contentW - textWidth);
                }

                float textY = contentY;
                if (style->text_alignment & NK_TEXT_ALIGN_BOTTOM) {
                    textY = contentY + std::max(0.0f, contentH - buttonFont->height);
                } else {
                    textY = contentY + std::max(0.0f, (contentH - buttonFont->height) * 0.5f);
                }

                struct nk_rect labelBounds = nk_rect(textX, textY, std::max(1.0f, contentW), buttonFont->height);
                if (pressed) {
                    labelBounds.x += 1.0f;
                    labelBounds.y += 1.0f;
                }
                nk_draw_text(&win->buffer, labelBounds, label, labelLength, buttonFont, nk_rgba(0, 0, 0, 0), labelColor);
            }

            return activated != 0;
        }

        void restore_last_text_color() {
            if (m_ctx == nullptr || m_textColorStack.empty()) {
                return;
            }
            m_ctx->style.text.color = m_textColorStack.back();
            m_textColorStack.pop_back();
        }

        void restore_last_panel_body_color() {
            if (m_ctx == nullptr || m_panelBodyColorStack.empty()) {
                return;
            }
            const PanelBodyStyleSnapshot snapshot = m_panelBodyColorStack.back();
            m_panelBodyColorStack.pop_back();
            m_ctx->style.window.background = snapshot.background;
            m_ctx->style.window.fixed_background = snapshot.fixedBackground;
        }

        void restore_last_panel_title_bar_color() {
            if (m_ctx == nullptr || m_panelTitleBarColorStack.empty()) {
                return;
            }
            const StyleItemStateSnapshot snapshot = m_panelTitleBarColorStack.back();
            m_panelTitleBarColorStack.pop_back();
            m_ctx->style.window.header.normal = snapshot.normal;
            m_ctx->style.window.header.hover = snapshot.hover;
            m_ctx->style.window.header.active = snapshot.active;
        }

        void restore_last_panel_border_color() {
            if (m_ctx == nullptr || m_panelBorderColorStack.empty()) {
                return;
            }
            m_ctx->style.window.border_color = m_panelBorderColorStack.back();
            m_panelBorderColorStack.pop_back();
        }

        void restore_last_panel_title_text_color() {
            if (m_ctx == nullptr || m_panelTitleTextColorStack.empty()) {
                return;
            }
            const ColorStateSnapshot snapshot = m_panelTitleTextColorStack.back();
            m_panelTitleTextColorStack.pop_back();
            m_ctx->style.window.header.label_normal = snapshot.normal;
            m_ctx->style.window.header.label_hover = snapshot.hover;
            m_ctx->style.window.header.label_active = snapshot.active;
        }

        void restore_last_radio_button_text_color() {
            if (m_ctx == nullptr || m_radioButtonTextColorStack.empty()) {
                return;
            }
            const ColorStateSnapshot snapshot = m_radioButtonTextColorStack.back();
            m_radioButtonTextColorStack.pop_back();
            m_ctx->style.option.text_normal = snapshot.normal;
            m_ctx->style.option.text_hover = snapshot.hover;
            m_ctx->style.option.text_active = snapshot.active;
        }

        void restore_last_button_background_color() {
            if (m_ctx == nullptr || m_buttonBackgroundColorStack.empty()) {
                return;
            }
            const StyleItemStateSnapshot snapshot = m_buttonBackgroundColorStack.back();
            m_buttonBackgroundColorStack.pop_back();
            m_ctx->style.button.normal = snapshot.normal;
            m_ctx->style.button.hover = snapshot.hover;
            m_ctx->style.button.active = snapshot.active;
        }

        void restore_last_button_hover_background_color() {
            if (m_ctx == nullptr || m_buttonHoverBackgroundColorStack.empty()) {
                return;
            }
            m_ctx->style.button.hover = m_buttonHoverBackgroundColorStack.back();
            m_buttonHoverBackgroundColorStack.pop_back();
        }

        void restore_last_button_clicked_background_color() {
            if (m_ctx == nullptr || m_buttonClickedBackgroundColorStack.empty()) {
                return;
            }
            m_ctx->style.button.active = m_buttonClickedBackgroundColorStack.back();
            m_buttonClickedBackgroundColorStack.pop_back();
        }

        void restore_last_button_height() {
            if (!m_buttonHeightStack.empty()) {
                m_buttonHeightStack.pop_back();
            }
        }

        void restore_last_button_width() {
            if (!m_buttonWidthStack.empty()) {
                m_buttonWidthStack.pop_back();
            }
        }

        void restore_last_button_corner_radius() {
            if (!m_buttonCornerRadiusStack.empty()) {
                m_buttonCornerRadiusStack.pop_back();
            }
        }

        void restore_last_button_border_thickness() {
            if (!m_buttonBorderThicknessStack.empty()) {
                m_buttonBorderThicknessStack.pop_back();
            }
        }

        void restore_last_button_padding() {
            if (!m_buttonPaddingStack.empty()) {
                m_buttonPaddingStack.pop_back();
            }
        }

        void restore_last_button_style() {
            if (!m_buttonStyleStack.empty()) {
                m_buttonStyleStack.pop_back();
            }
        }

        void restore_last_button_highlight_color() {
            if (!m_buttonHighlightColorStack.empty()) {
                m_buttonHighlightColorStack.pop_back();
            }
        }

        void restore_last_button_shadow_color() {
            if (!m_buttonShadowColorStack.empty()) {
                m_buttonShadowColorStack.pop_back();
            }
        }

        void restore_last_button_border_color() {
            if (m_ctx == nullptr || m_buttonBorderColorStack.empty()) {
                return;
            }
            m_ctx->style.button.border_color = m_buttonBorderColorStack.back();
            m_buttonBorderColorStack.pop_back();
        }

        void restore_last_button_text_color() {
            if (m_ctx == nullptr || m_buttonTextColorStack.empty()) {
                return;
            }
            const ColorStateSnapshot snapshot = m_buttonTextColorStack.back();
            m_buttonTextColorStack.pop_back();
            m_ctx->style.button.text_normal = snapshot.normal;
            m_ctx->style.button.text_hover = snapshot.hover;
            m_ctx->style.button.text_active = snapshot.active;
        }

        void restore_last_text_input_background_color() {
            if (m_ctx == nullptr || m_textInputBackgroundColorStack.empty()) {
                return;
            }
            const StyleItemStateSnapshot snapshot = m_textInputBackgroundColorStack.back();
            m_textInputBackgroundColorStack.pop_back();
            m_ctx->style.edit.normal = snapshot.normal;
            m_ctx->style.edit.hover = snapshot.hover;
            m_ctx->style.edit.active = snapshot.active;
        }

        void restore_last_text_input_text_color() {
            if (m_ctx == nullptr || m_textInputTextColorStack.empty()) {
                return;
            }
            const TextInputTextColorSnapshot snapshot = m_textInputTextColorStack.back();
            m_textInputTextColorStack.pop_back();
            m_ctx->style.edit.cursor_text_normal = snapshot.cursorTextNormal;
            m_ctx->style.edit.cursor_text_hover = snapshot.cursorTextHover;
            m_ctx->style.edit.text_normal = snapshot.textNormal;
            m_ctx->style.edit.text_hover = snapshot.textHover;
            m_ctx->style.edit.text_active = snapshot.textActive;
            m_ctx->style.edit.selected_text_normal = snapshot.selectedTextNormal;
            m_ctx->style.edit.selected_text_hover = snapshot.selectedTextHover;
            m_ctx->style.edit.cursor_normal = snapshot.cursorNormal;
            m_ctx->style.edit.cursor_hover = snapshot.cursorHover;
        }
        

    public:
        UIPainterImpl(nk_context* ctx, VkExtent2D extent) : m_ctx(ctx), m_framebufferExtent(extent) {}

        ~UIPainterImpl() override {
            while (!m_textInputTextColorStack.empty()) {
                restore_last_text_input_text_color();
            }
            while (!m_textInputBackgroundColorStack.empty()) {
                restore_last_text_input_background_color();
            }
            while (!m_buttonShadowColorStack.empty()) {
                restore_last_button_shadow_color();
            }
            while (!m_buttonHighlightColorStack.empty()) {
                restore_last_button_highlight_color();
            }
            while (!m_buttonStyleStack.empty()) {
                restore_last_button_style();
            }
            while (!m_buttonPaddingStack.empty()) {
                restore_last_button_padding();
            }
            while (!m_buttonBorderThicknessStack.empty()) {
                restore_last_button_border_thickness();
            }
            while (!m_buttonCornerRadiusStack.empty()) {
                restore_last_button_corner_radius();
            }
            while (!m_buttonWidthStack.empty()) {
                restore_last_button_width();
            }
            while (!m_buttonHeightStack.empty()) {
                restore_last_button_height();
            }
            while (!m_buttonTextColorStack.empty()) {
                restore_last_button_text_color();
            }
            while (!m_buttonBorderColorStack.empty()) {
                restore_last_button_border_color();
            }
            while (!m_buttonClickedBackgroundColorStack.empty()) {
                restore_last_button_clicked_background_color();
            }
            while (!m_buttonHoverBackgroundColorStack.empty()) {
                restore_last_button_hover_background_color();
            }
            while (!m_buttonBackgroundColorStack.empty()) {
                restore_last_button_background_color();
            }
            while (!m_radioButtonTextColorStack.empty()) {
                restore_last_radio_button_text_color();
            }
            while (!m_panelTitleTextColorStack.empty()) {
                restore_last_panel_title_text_color();
            }
            while (!m_panelBorderColorStack.empty()) {
                restore_last_panel_border_color();
            }
            while (!m_panelTitleBarColorStack.empty()) {
                restore_last_panel_title_bar_color();
            }
            while (!m_panelBodyColorStack.empty()) {
                restore_last_panel_body_color();
            }
            while (!m_textColorStack.empty()) {
                restore_last_text_color();
            }
        }

        bool begin_panel(const char* title, int x, int y, int width, int height) override;

        void end_panel() override;

        bool begin_panel_subsection(const char* title) override;

        void end_panel_subsection() override;

        Spherical::UIRect get_current_panel_bounds() const override;

        Spherical::UIRect get_current_panel_content_bounds() const override;

        bool begin_workspace_container(const char* title, int x, int y, int width, int height) override;

        void end_workspace_container() override;

        int get_workspace_panel_count(const char* containerTitle) const override;

        Spherical::DockLayout get_workspace_dock_layout(const char* containerTitle) const override;

        void undock_panel_from_workspace(const char* panelTitle) override;

        void label(const char* text) override;

        void push_font(Spherical::FontStyle style) override {
            const nk_user_font* font = Spherical::FontRenderer::GetFontHandle(style);
            if (font == nullptr && m_ctx != nullptr) {
                font = m_ctx->style.font;
            }
            if (font != nullptr) {
                nk_style_push_font(m_ctx, font);
            }
        }

        void pop_font() override {
            nk_style_pop_font(m_ctx);
        }

        void spacing() override;
        
        void slider_float(const char* label, float* value, float min, float max, float step) override;

        bool button(const char* label) override;

        void text_input(const char* label, char* buffer, size_t bufferSize) override;

        bool radio_button(const char* label, int* activeIndex, int value) override;
        
        uint32_t get_framebuffer_width() const override {
            return m_framebufferExtent.width;
        }

        uint32_t get_framebuffer_height() const override {
            return m_framebufferExtent.height;
        }
        
        void push_text_color(const Spherical::UIColor& color) override
        {
            if (m_ctx == nullptr) {
                return;
            }
            m_textColorStack.push_back(m_ctx->style.text.color);
            m_ctx->style.text.color = ToNkColor(color);
        };
        
        void pop_text_color() override {
            restore_last_text_color();
        };

        void push_panel_body_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_panelBodyColorStack.push_back({
                m_ctx->style.window.background,
                m_ctx->style.window.fixed_background
            });
            m_ctx->style.window.background = ToNkColor(color);
            m_ctx->style.window.fixed_background = ToNkStyleItem(color);
        };
        
        void pop_panel_body_color() override {
            restore_last_panel_body_color();
        };

        void push_panel_title_bar_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_panelTitleBarColorStack.push_back({
                m_ctx->style.window.header.normal,
                m_ctx->style.window.header.hover,
                m_ctx->style.window.header.active
            });

            const nk_style_item styleItem = ToNkStyleItem(color);
            m_ctx->style.window.header.normal = styleItem;
            m_ctx->style.window.header.hover = styleItem;
            m_ctx->style.window.header.active = styleItem;
        };

        void pop_panel_title_bar_color() override {
            restore_last_panel_title_bar_color();
        };

        void push_panel_border_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_panelBorderColorStack.push_back(m_ctx->style.window.border_color);
            m_ctx->style.window.border_color = ToNkColor(color);
        };

        void pop_panel_border_color() override {
            restore_last_panel_border_color();
        };

        void push_panel_title_text_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_panelTitleTextColorStack.push_back({
                m_ctx->style.window.header.label_normal,
                m_ctx->style.window.header.label_hover,
                m_ctx->style.window.header.label_active
            });

            const nk_color nkColor = ToNkColor(color);
            m_ctx->style.window.header.label_normal = nkColor;
            m_ctx->style.window.header.label_hover = nkColor;
            m_ctx->style.window.header.label_active = nkColor;
        };

        void pop_panel_title_text_color() override {
            restore_last_panel_title_text_color();
        };

        void push_radio_button_text_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_radioButtonTextColorStack.push_back({
                m_ctx->style.option.text_normal,
                m_ctx->style.option.text_hover,
                m_ctx->style.option.text_active
            });

            const nk_color nkColor = ToNkColor(color);
            m_ctx->style.option.text_normal = nkColor;
            m_ctx->style.option.text_hover = nkColor;
            m_ctx->style.option.text_active = nkColor;
        };

        void pop_radio_button_text_color() override {
            restore_last_radio_button_text_color();
        };

        void push_button_background_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_buttonBackgroundColorStack.push_back({
                m_ctx->style.button.normal,
                m_ctx->style.button.hover,
                m_ctx->style.button.active
            });

            const nk_style_item styleItem = ToNkStyleItem(color);
            m_ctx->style.button.normal = styleItem;
            m_ctx->style.button.hover = styleItem;
            m_ctx->style.button.active = styleItem;
        };

        void pop_button_background_color() override {
            restore_last_button_background_color();
        };

        void push_button_hover_background_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_buttonHoverBackgroundColorStack.push_back(m_ctx->style.button.hover);
            m_ctx->style.button.hover = ToNkStyleItem(color);
        };

        void pop_button_hover_background_color() override {
            restore_last_button_hover_background_color();
        };

        void push_button_clicked_background_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_buttonClickedBackgroundColorStack.push_back(m_ctx->style.button.active);
            m_ctx->style.button.active = ToNkStyleItem(color);
        };

        void pop_button_clicked_background_color() override {
            restore_last_button_clicked_background_color();
        };

        void push_button_height(float height) override {
            m_buttonHeightStack.push_back(std::max(1.0f, height));
        };

        void pop_button_height() override {
            restore_last_button_height();
        };

        void push_button_width(float width) override {
            m_buttonWidthStack.push_back(width);
        };

        void pop_button_width() override {
            restore_last_button_width();
        };

        void push_button_corner_radius(float radius) override {
            m_buttonCornerRadiusStack.push_back(std::max(0.0f, radius));
        };

        void pop_button_corner_radius() override {
            restore_last_button_corner_radius();
        };

        void push_button_border_thickness(float thickness) override {
            m_buttonBorderThicknessStack.push_back(std::max(0.0f, thickness));
        };

        void pop_button_border_thickness() override {
            restore_last_button_border_thickness();
        };

        void push_button_padding(const Spherical::UIVec2& padding) override {
            m_buttonPaddingStack.push_back(ToNkVec2({std::max(0.0f, padding.x), std::max(0.0f, padding.y)}));
        };

        void pop_button_padding() override {
            restore_last_button_padding();
        };

        void push_button_style(Spherical::ButtonStyle style) override {
            m_buttonStyleStack.push_back(style);
        };

        void pop_button_style() override {
            restore_last_button_style();
        };

        void push_button_highlight_color(const Spherical::UIColor& color) override {
            m_buttonHighlightColorStack.push_back(ToNkColor(color));
        };

        void pop_button_highlight_color() override {
            restore_last_button_highlight_color();
        };

        void push_button_shadow_color(const Spherical::UIColor& color) override {
            m_buttonShadowColorStack.push_back(ToNkColor(color));
        };

        void pop_button_shadow_color() override {
            restore_last_button_shadow_color();
        };

        void push_button_border_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_buttonBorderColorStack.push_back(m_ctx->style.button.border_color);
            m_ctx->style.button.border_color = ToNkColor(color);
        };

        void pop_button_border_color() override {
            restore_last_button_border_color();
        };

        void push_button_text_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_buttonTextColorStack.push_back({
                m_ctx->style.button.text_normal,
                m_ctx->style.button.text_hover,
                m_ctx->style.button.text_active
            });

            const nk_color nkColor = ToNkColor(color);
            m_ctx->style.button.text_normal = nkColor;
            m_ctx->style.button.text_hover = nkColor;
            m_ctx->style.button.text_active = nkColor;
        };

        void pop_button_text_color() override {
            restore_last_button_text_color();
        };

        void push_text_input_background_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_textInputBackgroundColorStack.push_back({
                m_ctx->style.edit.normal,
                m_ctx->style.edit.hover,
                m_ctx->style.edit.active
            });

            const nk_style_item styleItem = ToNkStyleItem(color);
            m_ctx->style.edit.normal = styleItem;
            m_ctx->style.edit.hover = styleItem;
            m_ctx->style.edit.active = styleItem;
        };

        void pop_text_input_background_color() override {
            restore_last_text_input_background_color();
        };

        void push_text_input_text_color(const Spherical::UIColor& color) override {
            if (m_ctx == nullptr) {
                return;
            }
            m_textInputTextColorStack.push_back({
                m_ctx->style.edit.cursor_normal,
                m_ctx->style.edit.cursor_hover,
                m_ctx->style.edit.cursor_text_normal,
                m_ctx->style.edit.cursor_text_hover,
                m_ctx->style.edit.text_normal,
                m_ctx->style.edit.text_hover,
                m_ctx->style.edit.text_active,
                m_ctx->style.edit.selected_text_normal,
                m_ctx->style.edit.selected_text_hover
            });

            const nk_color nkColor = ToNkColor(color);
            m_ctx->style.edit.cursor_text_normal = nkColor;
            m_ctx->style.edit.cursor_text_hover = nkColor;
            m_ctx->style.edit.text_normal = nkColor;
            m_ctx->style.edit.text_hover = nkColor;
            m_ctx->style.edit.text_active = nkColor;
            m_ctx->style.edit.selected_text_normal = nkColor;
            m_ctx->style.edit.selected_text_hover = nkColor;
        };

        void pop_text_input_text_color() override {
            restore_last_text_input_text_color();
        };
        
    };

    #include "ui/UIPainterPanel.inl"
    #include "ui/UIPainterWorkspaceContainer.inl"
    #include "ui/UIPainterLayout.inl"
    #include "ui/UIPainterSlider.inl"
    #include "ui/UIPainterButton.inl"
    #include "ui/UIPainterTextInput.inl"
    #include "ui/UIPainterRadio.inl"

}

namespace Spherical {
    static nk_context ctx = {};
    static bool initialized = false;
    static std::vector<unsigned char> nk_buffer_storage;
    static std::vector<unsigned char> nk_cmd_buffer_storage;

    static nk_user_font s_fallbackFont = {};
    static float FallbackFontWidth(nk_handle /*handle*/, float height, const char* /*text*/, int len) {
        return static_cast<float>(len) * (height * 0.54f);
    }

    static Runtime::UiState g_runtimeUiState{};

    static void RuntimeDrawCenterMarker(nk_context* context, const VkExtent2D& framebufferExtent, void* /*userData*/) {
        DrawCenterMarkerOverlay(context, framebufferExtent);
    }

    static void RuntimeBuildUi(nk_context* context, const VkExtent2D& framebufferExtent, void* /*userData*/) {
        g_cursorState.requested = CursorRequest::Default;
        if (g_uiBuildCallback != nullptr) {
            UIPainterImpl painter(context, framebufferExtent);
            g_uiBuildCallback(painter);
        }
    }

    static void RuntimeDrawDockPreview(nk_context* context, const VkExtent2D& framebufferExtent, void* /*userData*/) {
        DrawWorkspaceDockPreviewOverlay(context, framebufferExtent);
    }

    static void RuntimeApplyCursor(void* /*userData*/) {
        ApplyRequestedCursor();
    }

    static void RuntimeSyncTextInput(nk_context* context, void* /*userData*/) {
        SyncWindowTextInputState(context);
    }

    static RenderWatchdogStage ToWatchdogStage(Runtime::StageId stage) {
        switch (stage) {
            case Runtime::StageId::NewFrameEvents:
                return RenderWatchdogStage::NewFrameEvents;
            case Runtime::StageId::NewFrameTaskPoll:
                return RenderWatchdogStage::NewFrameTaskPoll;
            case Runtime::StageId::WaitFence:
                return RenderWatchdogStage::WaitFence;
            case Runtime::StageId::AcquireImage:
                return RenderWatchdogStage::AcquireImage;
            case Runtime::StageId::BuildUi:
                return RenderWatchdogStage::BuildUi;
            case Runtime::StageId::EndCommandBuffer:
                return RenderWatchdogStage::EndCommandBuffer;
            case Runtime::StageId::Submit:
                return RenderWatchdogStage::Submit;
            case Runtime::StageId::Present:
                return RenderWatchdogStage::Present;
            case Runtime::StageId::Idle:
            default:
                return RenderWatchdogStage::Idle;
        }
    }

    static void RuntimeSetStage(Runtime::StageId stage, void* /*userData*/) {
        SetRenderWatchdogStage(ToWatchdogStage(stage));
    }

    static void RuntimeLogSync(const char* stage, VkResult result, uint64_t frameId, void* /*userData*/) {
        LogRenderSyncEvent(stage, result, frameId);
    }

    bool Init(const SphericalInitInfo& info) {
        s_fallbackFont.width = FallbackFontWidth;
        s_fallbackFont.userdata = nk_handle_ptr(nullptr);

        Runtime::LifecycleState lifecycleState{};
        lifecycleState.backend = &g_backend;
        lifecycleState.ctx = &ctx;
        lifecycleState.initialized = &initialized;
        lifecycleState.textInputWasActive = &g_textInputWasActive;
        lifecycleState.nkBufferStorage = &nk_buffer_storage;
        lifecycleState.nkCmdBufferStorage = &nk_cmd_buffer_storage;
        lifecycleState.fallbackFont = &s_fallbackFont;

        Runtime::LifecycleHooks lifecycleHooks{};
        lifecycleHooks.resolveUiScale = [](const SphericalInitInfo& initInfo, SDL_Window* window, void* /*userData*/) {
            return ResolveUiScale(initInfo, window);
        };
        lifecycleHooks.resetRuntimeDiagLog = [](void* /*userData*/) {
            ResetRuntimeDiagLog();
        };
        lifecycleHooks.startRenderWatchdog = [](void* /*userData*/) {
            StartRenderWatchdog();
        };
        lifecycleHooks.stopRenderWatchdog = [](void* /*userData*/) {
            StopRenderWatchdog();
        };

        if (!Runtime::InitLifecycle(info, lifecycleState, lifecycleHooks)) {
            return false;
        }

        Runtime::PrimeFrameTimer(g_runtimeUiState);
        return true;
    }

    void NewFrame() {
        if (!initialized) {
            return;
        }

        Runtime::RuntimeFacadeContext runtimeContext{};
        runtimeContext.ctx = &ctx;
        runtimeContext.backend = &g_backend;
        runtimeContext.nkCmdBufferStorage = &nk_cmd_buffer_storage;
        runtimeContext.frameCounter = &g_renderFrameCounter;
        runtimeContext.uiState = &g_runtimeUiState;
        runtimeContext.uiHooks.drawCenterMarker = RuntimeDrawCenterMarker;
        runtimeContext.uiHooks.buildUi = RuntimeBuildUi;
        runtimeContext.uiHooks.drawDockPreview = RuntimeDrawDockPreview;
        runtimeContext.uiHooks.applyCursor = RuntimeApplyCursor;
        runtimeContext.uiHooks.syncTextInput = RuntimeSyncTextInput;
        runtimeContext.setStage = RuntimeSetStage;
        runtimeContext.logSync = RuntimeLogSync;
        Runtime::RunNewFrame(runtimeContext, initialized);
    }

    void Render() {
        Runtime::RuntimeFacadeContext runtimeContext{};
        runtimeContext.ctx = &ctx;
        runtimeContext.backend = &g_backend;
        runtimeContext.nkCmdBufferStorage = &nk_cmd_buffer_storage;
        runtimeContext.frameCounter = &g_renderFrameCounter;
        runtimeContext.uiState = &g_runtimeUiState;
        runtimeContext.uiHooks.drawCenterMarker = RuntimeDrawCenterMarker;
        runtimeContext.uiHooks.buildUi = RuntimeBuildUi;
        runtimeContext.uiHooks.drawDockPreview = RuntimeDrawDockPreview;
        runtimeContext.uiHooks.applyCursor = RuntimeApplyCursor;
        runtimeContext.uiHooks.syncTextInput = RuntimeSyncTextInput;
        runtimeContext.setStage = RuntimeSetStage;
        runtimeContext.logSync = RuntimeLogSync;
        Runtime::RunRender(runtimeContext, initialized);
    }

    void Shutdown() {
        Runtime::LifecycleState lifecycleState{};
        lifecycleState.backend = &g_backend;
        lifecycleState.ctx = &ctx;
        lifecycleState.initialized = &initialized;
        lifecycleState.textInputWasActive = &g_textInputWasActive;
        lifecycleState.nkBufferStorage = &nk_buffer_storage;
        lifecycleState.nkCmdBufferStorage = &nk_cmd_buffer_storage;
        lifecycleState.fallbackFont = &s_fallbackFont;

        Runtime::LifecycleHooks lifecycleHooks{};
        lifecycleHooks.stopRenderWatchdog = [](void* /*userData*/) {
            StopRenderWatchdog();
        };
        Runtime::ShutdownLifecycle(lifecycleState, lifecycleHooks);

        ShutdownCursors();
        g_panelDrag = {};
        g_panelStates.clear();
        g_uiBuildCallback = nullptr;  // Release lambda captures and prevent stale callbacks on re-init
        initialized = false;
    }

    void SetDockModelFunctionTable(const DockModelFunctionTable* table) {
        DockModelBridge::SetFunctionTable(table);
    }

    DockModelFunctionTable GetDockModelFunctionTable() {
        return DockModelBridge::GetFunctionTable();
    }

    bool ExportWorkspaceModel(const char* containerTitle, WorkspaceContainerModel& outModel) {
        if (containerTitle == nullptr || containerTitle[0] == '\0') {
            return false;
        }

        const auto it = g_workspaceContainerStates.find(containerTitle);
        if (it == g_workspaceContainerStates.end() || !it->second.initialized) {
            return false;
        }

        DockModelBridge::ExportWorkspaceContainerState(it->second, outModel);
        const DockModelFunctionTable table = DockModelBridge::GetFunctionTable();
        if (table.validate != nullptr && !table.validate(outModel, table.userData)) {
            return false;
        }
        return true;
    }

    bool ImportWorkspaceModel(const char* containerTitle, const WorkspaceContainerModel& model) {
        if (containerTitle == nullptr || containerTitle[0] == '\0') {
            return false;
        }

        if (!DockModelBridge::ValidateWorkspaceContainerModel(model)) {
            return false;
        }

        const DockModelFunctionTable table = DockModelBridge::GetFunctionTable();
        if (table.validate != nullptr && !table.validate(model, table.userData)) {
            return false;
        }

        WorkspaceContainerState& state = g_workspaceContainerStates[containerTitle];
        DockModelBridge::ApplyWorkspaceContainerModel(model, state);

        if (state.root != nullptr) {
            CollapseDockGroups(state.root);
            PruneWorkspaceSplitterLinks(state);
            PruneWorkspaceSplitterIntersections(state);
            if (state.root == nullptr) {
                state.pendingSplitterReconcile = false;
            }
        } else {
            state.pendingSplitterReconcile = false;
        }

        return true;
    }

    bool SaveWorkspaceModel(const char* containerTitle) {
        WorkspaceContainerModel model;
        if (!ExportWorkspaceModel(containerTitle, model)) {
            return false;
        }

        return DockModelBridge::SaveWorkspaceModel(containerTitle, model);
    }

    bool LoadWorkspaceModel(const char* containerTitle) {
        WorkspaceContainerModel loadedModel;
        if (!DockModelBridge::LoadWorkspaceModel(containerTitle, loadedModel)) {
            return false;
        }

        return ImportWorkspaceModel(containerTitle, loadedModel);
    }

    void RegisterUI(const UIBuildFn& callback) {
        g_uiBuildCallback = callback;
    }
}
