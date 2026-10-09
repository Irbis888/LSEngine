#include "JobSystem.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
    enki::TaskPriority ToEnkiPriority(JobSystem::Priority priority)
    {
        switch (priority)
        {
        case JobSystem::Priority::High: return enki::TASK_PRIORITY_HIGH;
        case JobSystem::Priority::Normal: return enki::TASK_PRIORITY_MED;
        case JobSystem::Priority::Low: return enki::TASK_PRIORITY_LOW;
        }
        throw std::invalid_argument("Invalid job priority");
    }
    struct ExecutionContext
    {
        JobSystem* system = nullptr;
        const enki::ITaskSet* task = nullptr;
    };
    thread_local ExecutionContext gExecution;

    struct ExecutionScope
    {
        ExecutionContext previous = gExecution;
        ExecutionScope(JobSystem* system, const enki::ITaskSet* task)
        {
            gExecution = { system, task };
        }
        ~ExecutionScope() { gExecution = previous; }
    };
}

struct JobSystem::Task final : enki::ITaskSet
{
    Task(JobSystem* owner, uint32_t count, uint32_t minRange, RangeJob job)
        : enki::ITaskSet(count, minRange), owner(owner), job(std::move(job)) {}

    void ExecuteRange(enki::TaskSetPartition range, uint32_t) override
    {
        ExecutionScope scope(owner, this);
        try
        {
            job(range.start, range.end);
        }
        catch (...)
        {
            // Exceptions must not escape into enkiTS worker threads.
            std::lock_guard lock(errorMutex);
            if (!error) error = std::current_exception();
        }
    }

    std::exception_ptr GetError(bool observe = false)
    {
        std::lock_guard lock(errorMutex);
        if (observe) errorObserved = true;
        return error;
    }

    bool CanRelease()
    {
        if (!submitted.load(std::memory_order_acquire) || !GetIsComplete()) return false;
        std::lock_guard lock(errorMutex);
        return !error || errorObserved;
    }

    JobSystem* const owner;
    RangeJob job;
    // Initially enkiTS reports complete, until AddTaskSetToPipe is called.
    // Keep this task alive while submission is still in progress.
    std::atomic<bool> submitted{ false };
    std::mutex errorMutex;
    std::exception_ptr error;
    bool errorObserved = false;
};

JobSystem::TaskHandle::TaskHandle(std::shared_ptr<Task> task) : mTask(std::move(task)) {}

bool JobSystem::TaskHandle::IsComplete() const
{
    return !mTask || (mTask->submitted.load(std::memory_order_acquire) && mTask->GetIsComplete());
}

JobSystem::~JobSystem()
{
    Shutdown();
}

void JobSystem::Init(uint32_t workerThreadCount)
{
    if (IsInitialized()) throw std::logic_error("JobSystem is already initialized");
    if (gExecution.system) throw std::logic_error("Cannot initialize a JobSystem inside a job");
    if (workerThreadCount == std::numeric_limits<uint32_t>::max())
        throw std::invalid_argument("Worker count leaves no room for the owner thread");
    if (workerThreadCount == 0)
    {
        const uint32_t hardwareThreads = std::thread::hardware_concurrency();
        workerThreadCount = hardwareThreads > 1 ? hardwareThreads - 1 : 1;
    }
    enki::TaskSchedulerConfig config;
    config.numTaskThreadsToCreate = workerThreadCount;
    mOwnerThread = std::this_thread::get_id();
    mScheduler.Initialize(config);
    mWorkerThreadCount.store(workerThreadCount, std::memory_order_relaxed);
    mState.store(State::Running, std::memory_order_release);
}

void JobSystem::Shutdown()
{
    if (!IsInitialized()) return;
    RequireOwnerThread();
    mState.store(State::Stopping, std::memory_order_release);
    // Existing jobs may still submit child work while the scheduler drains.
    mScheduler.WaitforAllAndShutdown();
    std::vector<std::shared_ptr<Task>> completed;
    {
        std::lock_guard lock(mTasksMutex);
        completed.swap(mTasks);
    }
    mWorkerThreadCount.store(0, std::memory_order_relaxed);
    mState.store(State::Stopped, std::memory_order_release);
    // Handles remain valid after shutdown. Wait(handle) can still report a
    // captured exception; Shutdown and destruction only drain/join work.
}

JobSystem::TaskHandle JobSystem::Submit(Job job, Priority priority)
{
    if (!job) throw std::invalid_argument("Cannot submit an empty job");
    return Schedule(1, 1, [job = std::move(job)](uint32_t, uint32_t) { job(); }, priority);
}

JobSystem::TaskHandle JobSystem::Dispatch(uint32_t itemCount, RangeJob job, uint32_t minRange, Priority priority)
{
    if (!job) throw std::invalid_argument("Cannot dispatch an empty job");
    if (!minRange) throw std::invalid_argument("Dispatch minRange must be greater than zero");
    return Schedule(itemCount, minRange, std::move(job), priority);
}

JobSystem::TaskHandle JobSystem::Schedule(uint32_t itemCount, uint32_t minRange, RangeJob job, Priority priority)
{
    RequireTaskThread();
    CollectCompletedTasks();
    auto task = std::make_shared<Task>(this, itemCount, minRange, std::move(job));
    task->m_Priority = ToEnkiPriority(priority);
    if (itemCount != 0)
    {
        {
            std::lock_guard lock(mTasksMutex);
            mTasks.push_back(task);
        }
        // Do not hold mTasksMutex: a full enkiTS pipe can execute work here,
        // and that work can submit more jobs or wait for a child task.
        mScheduler.AddTaskSetToPipe(task.get());
    }
    task->submitted.store(true, std::memory_order_release);
    return TaskHandle(std::move(task));
}

void JobSystem::Wait(const TaskHandle& handle, Priority lowestToRun)
{
    if (!handle.mTask) return;
    const auto& task = handle.mTask;
    if (task->owner != this) throw std::invalid_argument("Task belongs to another JobSystem");
    if (gExecution.task == task.get()) throw std::logic_error("A job cannot wait for itself");
    const auto lowest = ToEnkiPriority(lowestToRun);
    if (task->m_Priority > lowest)
        throw std::invalid_argument("Wait priority excludes the target task");
    if (!handle.IsComplete())
    {
        RequireTaskThread();
        mScheduler.WaitforTask(task.get(), lowest);
    }
    const auto error = task->GetError(true);
    CollectCompletedTasks();
    if (error) std::rethrow_exception(error);
}

void JobSystem::RunHighPriorityTasks()
{
    RequireTaskThread();
    mScheduler.WaitforTask(nullptr, enki::TASK_PRIORITY_HIGH);
}

void JobSystem::WaitAll()
{
    if (!IsInitialized()) return;
    RequireOwnerThread();
    mScheduler.WaitforAll();
    std::vector<std::shared_ptr<Task>> completed;
    {
        std::lock_guard lock(mTasksMutex);
        completed.swap(mTasks);
    }
    std::exception_ptr firstError;
    for (const auto& task : completed)
    {
        const auto error = task->GetError(true);
        if (!firstError) firstError = error;
    }
    if (firstError) std::rethrow_exception(firstError);
}

bool JobSystem::IsInitialized() const
{
    return mState.load(std::memory_order_acquire) != State::Stopped;
}

uint32_t JobSystem::GetWorkerThreadCount() const
{
    return mWorkerThreadCount.load(std::memory_order_relaxed);
}

void JobSystem::RequireOwnerThread() const
{
    if (std::this_thread::get_id() != mOwnerThread || gExecution.system)
        throw std::logic_error("JobSystem lifecycle/WaitAll must run on Init's thread outside a job");
}

void JobSystem::RequireTaskThread() const
{
    const State state = mState.load(std::memory_order_acquire);
    const bool insideJob = gExecution.system == this;
    if (state == State::Stopped || (state == State::Stopping && !insideJob))
        throw std::logic_error("JobSystem is not accepting work");
    if (!insideJob && std::this_thread::get_id() != mOwnerThread)
        throw std::logic_error("JobSystem call from an unregistered external thread");
}

void JobSystem::CollectCompletedTasks()
{
    // Retire outside the mutex so destruction of callback captures never
    // happens while the submission mutex is held.
    std::vector<std::shared_ptr<Task>> retired;
    {
        std::lock_guard lock(mTasksMutex);
        auto first = std::remove_if(mTasks.begin(), mTasks.end(), [&](const auto& task)
        {
            if (!task->CanRelease()) return false;
            retired.push_back(task);
            return true;
        });
        mTasks.erase(first, mTasks.end());
    }
}
