#include "JobSystem.h"
#include <TaskScheduler.h>
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <thread>
#include <vector>

class JobSystem::Job
{
    friend class JobSystem;
    std::unique_ptr<enki::TaskSet> cpu;
    std::unique_ptr<enki::LambdaPinnedTask> loading;
    std::exception_ptr error;
    enki::ICompletable* Task() const { return cpu ? static_cast<enki::ICompletable*>(cpu.get()) : loading.get(); }
};
struct JobSystem::Impl
{
    enki::TaskScheduler cpu, loading;
    std::vector<Handle> jobs;
    std::thread::id owner;
    uint32_t loadingWorkers = 0, nextWorker = 0;
    bool initialized = false;
    void CheckOwner() const {
        if (!initialized || owner != std::this_thread::get_id())
            throw std::logic_error("JobSystem API must run on its initialized owner thread");
    }
};
JobSystem::JobSystem() : impl(std::make_unique<Impl>()) {}
JobSystem::~JobSystem() { Shutdown(); }
JobSystem& JobSystem::Get() { static JobSystem instance; return instance; }
void JobSystem::Init(uint32_t cpuWorkers, uint32_t loadingWorkers)
{
    if (impl->initialized) { impl->CheckOwner(); return; }
    impl->owner = std::this_thread::get_id();
    if (!cpuWorkers) cpuWorkers = std::min(8u, std::max(1u, std::thread::hardware_concurrency() > 3 ? std::thread::hardware_concurrency() - 3 : 1));
    impl->loadingWorkers = std::max(1u, loadingWorkers);
    impl->cpu.Initialize(cpuWorkers + 1);
    impl->loading.Initialize(impl->loadingWorkers + 1);
    impl->initialized = true;
}
JobSystem::Handle JobSystem::Submit(std::function<void()> work, Queue queue)
{
    impl->CheckOwner();
    auto job = std::make_shared<Job>();
    auto run = [jobPtr = job.get(), work = std::move(work)] {
        try { work(); } catch (...) { jobPtr->error = std::current_exception(); }
    };
    if (queue == Queue::Loading)
        job->loading = std::make_unique<enki::LambdaPinnedTask>(1 + impl->nextWorker++ % impl->loadingWorkers, std::move(run));
    else
        job->cpu = std::make_unique<enki::TaskSet>([run = std::move(run)](enki::TaskSetPartition, uint32_t) { run(); });
    impl->jobs.push_back(job);
    if (job->loading) impl->loading.AddPinnedTask(job->loading.get());
    else impl->cpu.AddTaskSetToPipe(job->cpu.get());
    return job;
}
bool JobSystem::IsComplete(const Handle& job) const { return !job || job->Task()->GetIsComplete(); }
void JobSystem::Wait(const Handle& job)
{
    impl->CheckOwner();
    if (!job) return;
    (job->loading ? impl->loading : impl->cpu).WaitforTask(job->Task());
    if (job->error) std::rethrow_exception(job->error);
}
void JobSystem::Collect()
{
    impl->CheckOwner();
    std::erase_if(impl->jobs, [](const Handle& job) { return job->Task()->GetIsComplete(); });
}
void JobSystem::Shutdown()
{
    if (!impl->initialized) return;
    impl->CheckOwner();
    impl->loading.WaitforAllAndShutdown();
    impl->cpu.WaitforAllAndShutdown();
    impl->jobs.clear();
    impl->initialized = false;
}
