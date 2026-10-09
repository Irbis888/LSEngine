#pragma once

#include <TaskScheduler.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// CPU scheduling only. Lifecycle operations belong to the thread that calls
// Init. Submit, Dispatch and Wait may also be called from this system's jobs;
// unrelated external threads are not registered with enkiTS.
class JobSystem
{
    struct Task;

public:
    class TaskHandle
    {
    public:
        TaskHandle() = default;
        bool IsComplete() const;
        explicit operator bool() const { return mTask != nullptr; }

    private:
        friend class JobSystem;
        explicit TaskHandle(std::shared_ptr<Task> task);
        std::shared_ptr<Task> mTask;
    };

    using Job = std::function<void()>;
    // Each callback processes [begin, end); ranges may execute concurrently

    using RangeJob = std::function<void(uint32_t begin, uint32_t end)>;

    JobSystem() = default;
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    // 0 selects max(1, hardware_concurrency - 1) workers, excluding Init's thread.
    void Init(uint32_t workerThreadCount = 0);
    // Drains submitted work (including child jobs) and joins the workers.
    // Idempotent; the destructor also calls Shutdown on Init's thread.
    void Shutdown();

    // enkiTS can execute a callback on the submitting thread if its pipe is full.
    TaskHandle Submit(Job job);
    TaskHandle Dispatch(uint32_t itemCount, RangeJob job, uint32_t minRange = 64);

    // Wait can help execute other jobs on the calling thread. A callback's
    // exception is captured and rethrown here after its task has completed.
    void Wait(const TaskHandle& task);
    // Owner thread only, outside a job. Drains all work before reporting the
    // first unobserved task exception. Callbacks must eventually finish.
    void WaitAll();

    bool IsInitialized() const;
    uint32_t GetWorkerThreadCount() const;

private:
    enum class State { Stopped, Running, Stopping };
    void RequireOwnerThread() const;
    void RequireTaskThread() const;
    TaskHandle Schedule(uint32_t itemCount, uint32_t minRange, RangeJob job);
    void CollectCompletedTasks();

    enki::TaskScheduler mScheduler;
    std::atomic<State> mState{ State::Stopped };
    std::atomic<uint32_t> mWorkerThreadCount{ 0 };
    std::thread::id mOwnerThread;
    std::mutex mTasksMutex;
    std::vector<std::shared_ptr<Task>> mTasks;
};

