#include "TaskRunner.h"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdio>

namespace {
    struct Task {
        uint64_t id;
        std::function<void()> work;
        std::function<void()> completion;
    };

    class TaskRunnerState {
    public:
        TaskRunnerState() : nextTaskId(1), workerThread(nullptr), shutdown(false), executingTaskId(0) {}

        std::queue<Task> pendingQueue;
        std::mutex pendingMutex;

        std::queue<std::function<void()>> completedQueue;
        std::mutex completedMutex;

        std::condition_variable workerSignal;

        std::atomic<uint64_t> nextTaskId;
        std::unique_ptr<std::thread> workerThread;
        std::atomic<bool> shutdown;

        // ID of the task currently executing on the worker thread (0 = none).
        std::atomic<uint64_t> executingTaskId;

        void WorkerLoop() {
            while (!shutdown.load(std::memory_order_relaxed)) {
                Task task{};
                bool hasTask = false;

                {
                    std::unique_lock<std::mutex> lock(pendingMutex);
                    // Wait for work or shutdown signal
                    workerSignal.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                        return !pendingQueue.empty() || shutdown.load(std::memory_order_relaxed);
                    });

                    if (!pendingQueue.empty()) {
                        task = pendingQueue.front();
                        pendingQueue.pop();
                        hasTask = true;
                    }
                }

                if (hasTask) {
                    // Mark this task as currently executing so IsPending() sees it
                    executingTaskId.store(task.id, std::memory_order_relaxed);

                    // Execute work on worker thread
                    try {
                        if (task.work) {
                            task.work();
                        }
                    } catch (const std::exception& e) {
                        fprintf(stderr, "[TaskRunner] Exception in work function: %s\n", e.what());
                    } catch (...) {
                        fprintf(stderr, "[TaskRunner] Unknown exception in work function\n");
                    }

                    // Queue completion callback for main thread
                    if (task.completion) {
                        {
                            std::unique_lock<std::mutex> lock(completedMutex);
                            completedQueue.push(task.completion);
                        }
                    }

                    // Clear executing task ID
                    executingTaskId.store(0, std::memory_order_relaxed);
                }
            }
        }
    };

    TaskRunnerState* g_taskRunnerState = nullptr;
}

namespace Spherical {
namespace TaskRunner {
    void Init() {
        if (g_taskRunnerState != nullptr) {
            return;  // Already initialized
        }

        g_taskRunnerState = new TaskRunnerState();
        g_taskRunnerState->workerThread = std::make_unique<std::thread>(
            [](TaskRunnerState* state) {
                state->WorkerLoop();
            },
            g_taskRunnerState
        );
    }

    void Shutdown() {
        if (g_taskRunnerState == nullptr) {
            return;  // Not initialized
        }

        // Signal worker thread to stop
        g_taskRunnerState->shutdown.store(true, std::memory_order_relaxed);
        g_taskRunnerState->workerSignal.notify_one();

        if (g_taskRunnerState->workerThread) {
            g_taskRunnerState->workerThread->join();
        }

        // Flush any remaining completed callbacks
        {
            std::unique_lock<std::mutex> lock(g_taskRunnerState->completedMutex);
            while (!g_taskRunnerState->completedQueue.empty()) {
                auto callback = g_taskRunnerState->completedQueue.front();
                g_taskRunnerState->completedQueue.pop();
                lock.unlock();

                try {
                    if (callback) {
                        callback();
                    }
                } catch (const std::exception& e) {
                    fprintf(stderr, "[TaskRunner] Exception in completion callback: %s\n", e.what());
                } catch (...) {
                    fprintf(stderr, "[TaskRunner] Unknown exception in completion callback\n");
                }

                lock.lock();
            }
        }

        // Check if any tasks were dropped
        {
            std::unique_lock<std::mutex> lock(g_taskRunnerState->pendingMutex);
            if (!g_taskRunnerState->pendingQueue.empty()) {
                size_t dropped = g_taskRunnerState->pendingQueue.size();
                fprintf(stderr, "[TaskRunner] Warning: %zu pending tasks dropped on shutdown\n", dropped);
            }
        }

        delete g_taskRunnerState;
        g_taskRunnerState = nullptr;
    }

    uint64_t Submit(std::function<void()> workFn, std::function<void()> completionFn) {
        if (g_taskRunnerState == nullptr) {
            fprintf(stderr, "[TaskRunner] Submit called before Init(); ignoring task\n");
            return 0;
        }

        uint64_t taskId = g_taskRunnerState->nextTaskId.fetch_add(1, std::memory_order_relaxed);

        Task task{taskId, workFn, completionFn};

        {
            std::unique_lock<std::mutex> lock(g_taskRunnerState->pendingMutex);
            g_taskRunnerState->pendingQueue.push(task);
        }

        // Wake up worker thread
        g_taskRunnerState->workerSignal.notify_one();

        return taskId;
    }

    void Poll() {
        if (g_taskRunnerState == nullptr) {
            return;
        }

        std::queue<std::function<void()>> completedCallbacks;

        {
            std::unique_lock<std::mutex> lock(g_taskRunnerState->completedMutex);
            std::swap(completedCallbacks, g_taskRunnerState->completedQueue);
        }

        // Invoke callbacks on main thread
        while (!completedCallbacks.empty()) {
            auto callback = completedCallbacks.front();
            completedCallbacks.pop();

            try {
                if (callback) {
                    callback();
                }
            } catch (const std::exception& e) {
                fprintf(stderr, "[TaskRunner] Exception in completion callback: %s\n", e.what());
            } catch (...) {
                fprintf(stderr, "[TaskRunner] Unknown exception in completion callback\n");
            }
        }
    }

    bool IsPending(uint64_t taskId) {
        if (g_taskRunnerState == nullptr) {
            return false;
        }

        // Check if this task is currently executing on the worker thread
        if (g_taskRunnerState->executingTaskId.load(std::memory_order_relaxed) == taskId) {
            return true;
        }

        // Check pending queue
        {
            std::unique_lock<std::mutex> lock(g_taskRunnerState->pendingMutex);
            // Linear search (fine for typical small queue sizes)
            std::queue<Task> temp = g_taskRunnerState->pendingQueue;
            while (!temp.empty()) {
                if (temp.front().id == taskId) {
                    return true;
                }
                temp.pop();
            }
        }

        return false;
    }

} // namespace TaskRunner
} // namespace Spherical







