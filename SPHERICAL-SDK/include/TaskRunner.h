#pragma once

#include <functional>
#include <cstdint>

namespace Spherical {
namespace TaskRunner {
    /**
     * @brief Initialize the task runner with a single background worker thread.
     * Safe to call multiple times (subsequent calls after first are no-ops).
     */
    void Init();

    /**
     * @brief Shutdown the task runner and wait for pending work to complete.
     * Safe to call multiple times.
     */
    void Shutdown();

    /**
     * @brief Submit background work to be executed on the worker thread.
     * @param workFn Function executed on worker thread (must not touch Nuklear, Vulkan, or SDL state).
     * @param completionFn Optional callback executed on main thread after workFn completes.
     * @return Unique task ID for tracking (optional).
     * 
     * Thread-safe: safe to call from any thread, including the main render thread.
     */
    uint64_t Submit(std::function<void()> workFn, std::function<void()> completionFn = nullptr);

    /**
     * @brief Poll for completed tasks and dispatch main-thread callbacks.
     * Must be called once per frame on the main thread, typically in NewFrame().
     * Callbacks are invoked sequentially and synchronously; keep them quick.
     */
    void Poll();

    /**
     * @brief Check if a task is still pending (optional utility).
     * @return true if the task is queued or executing; false if completed or unknown ID.
     */
    bool IsPending(uint64_t taskId);

} // namespace TaskRunner
} // namespace Spherical

