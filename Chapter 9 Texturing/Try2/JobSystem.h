#pragma once
#include <cstdint>
#include <functional>
#include <memory>

class JobSystem
{
public:
    enum class Queue { CPU, Loading };
    class Job;
    using Handle = std::shared_ptr<Job>;
    static JobSystem& Get();
    JobSystem();
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    // Call all API methods on the owner thread. Callbacks run on workers.
    void Init(uint32_t cpuWorkers = 0, uint32_t loadingWorkers = 2);
    Handle Submit(std::function<void()> work, Queue queue = Queue::CPU);
    bool IsComplete(const Handle& job) const;
    void Wait(const Handle& job);
    void Collect();
    void Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;

};

