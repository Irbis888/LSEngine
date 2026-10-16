#include "JobSystem.h"

#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
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
    // Runs on the thread that completes the task, after enkiTS has stopped
    // using it, and drops the task's reference to itself.
    struct Completion final : enki::ICompletable
    {
        explicit Completion(Task* task) : task(task) {}

        void OnDependenciesComplete(enki::TaskScheduler* scheduler, uint32_t threadNum) override
        {
            enki::ICompletable::OnDependenciesComplete(scheduler, threadNum);
            // May destroy this Completion; enkiTS allows that here.
            task->Finish();
        }

        Task* const task;
    };

    Task(JobSystem* owner, uint32_t count, uint32_t minRange, RangeJob job)
        : enki::ITaskSet(count, minRange), owner(owner), job(std::move(job)), completion(this)
    {
        completion.SetDependency(dependency, this);
    }

    void Finish()
    {
        std::shared_ptr<Task> keepAlive = std::move(self);
        bool unobservedError;
        {
            std::lock_guard lock(errorMutex);
            unobservedError = error && !errorObserved;
        }
        if (unobservedError)
        {
            std::lock_guard lock(owner->mFailedTasksMutex);
            owner->mFailedTasks.push_back(keepAlive);
        }
    }

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

    std::exception_ptr ObserveError()
    {
        std::lock_guard lock(errorMutex);
        errorObserved = true;
        return error;
    }

    std::exception_ptr TakeUnobservedError()
    {
        std::lock_guard lock(errorMutex);
        if (errorObserved) return nullptr;
        errorObserved = true;
        return error;
    }

    JobSystem* const owner;
    RangeJob job;
    // Owns the task while enkiTS holds a raw pointer to it; cleared by Finish.
    std::shared_ptr<Task> self;
    std::mutex errorMutex;
    std::exception_ptr error;
    bool errorObserved = false;
    // Declared after completion so it is destroyed first.
    Completion completion;
    enki::Dependency dependency;
};

JobSystem::TaskHandle::TaskHandle(std::shared_ptr<Task> task) : mTask(std::move(task)) {}

bool JobSystem::TaskHandle::IsComplete() const
{
    return !mTask || mTask->GetIsComplete();
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
    std::vector<std::shared_ptr<Task>> failed;
    {
        std::lock_guard lock(mFailedTasksMutex);
        failed.swap(mFailedTasks);
    }
    mWorkerThreadCount.store(0, std::memory_order_relaxed);
    mState.store(State::Stopped, std::memory_order_release);
    // Handles remain valid after shutdown. Wait(handle) can still report a
    // captured exception; Shutdown and destruction only drain/join work.
}

JobSystem::TaskHandle JobSystem::Submit(Job job)
{
    if (!job) throw std::invalid_argument("Cannot submit an empty job");
    return Schedule(1, 1, [job = std::move(job)](uint32_t, uint32_t) { job(); });
}

JobSystem::TaskHandle JobSystem::Dispatch(uint32_t itemCount, RangeJob job, uint32_t minRange)
{
    if (!job) throw std::invalid_argument("Cannot dispatch an empty job");
    if (!minRange) throw std::invalid_argument("Dispatch minRange must be greater than zero");
    return Schedule(itemCount, minRange, std::move(job));
}

JobSystem::TaskHandle JobSystem::Schedule(uint32_t itemCount, uint32_t minRange, RangeJob job)
{
    RequireTaskThread();
    auto task = std::make_shared<Task>(this, itemCount, minRange, std::move(job));
    if (itemCount != 0)
    {
        // A full enkiTS pipe can run the task (and Finish) right here.
        task->self = task;
        mScheduler.AddTaskSetToPipe(task.get());
    }
    return TaskHandle(std::move(task));
}

void JobSystem::Wait(const TaskHandle& handle)
{
    if (!handle.mTask) return;
    const auto& task = handle.mTask;
    if (task->owner != this) throw std::invalid_argument("Task belongs to another JobSystem");
    if (gExecution.task == task.get()) throw std::logic_error("A job cannot wait for itself");
    if (!handle.IsComplete())
    {
        RequireTaskThread();
        mScheduler.WaitforTask(task.get());
    }
    const auto error = task->ObserveError();
    if (error) std::rethrow_exception(error);
}

void JobSystem::WaitAll()
{
    if (!IsInitialized()) return;
    RequireOwnerThread();
    // Completion actions run on worker threads before WaitforAll sees them idle.
    mScheduler.WaitforAll();
    std::vector<std::shared_ptr<Task>> failed;
    {
        std::lock_guard lock(mFailedTasksMutex);
        failed.swap(mFailedTasks);
    }
    std::exception_ptr firstError;
    for (const auto& task : failed)
    {
        const auto error = task->TakeUnobservedError();
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
