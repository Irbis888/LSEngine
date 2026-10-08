#include "JobSystem.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<typename Exception, typename F>
static void ExpectThrow(F&& fn, const char* message)
{
    bool threw = false;
    try { fn(); } catch (const Exception&) { threw = true; }
    Check(threw, message);
}

static void WaitUntil(const std::atomic<bool>& ready)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready.load(std::memory_order_acquire))
    {
        Check(std::chrono::steady_clock::now() < deadline, "Worker did not start in time");
        std::this_thread::yield();
    }
}

static void TestSubmitAndDispatch()
{
    JobSystem jobs;
    jobs.Shutdown();
    jobs.WaitAll();
    jobs.Wait({});
    Check(JobSystem::TaskHandle().IsComplete(), "Empty handle should be complete");
    ExpectThrow<std::logic_error>([&] { jobs.Submit([] {}); }, "Submit before Init accepted");
    jobs.Init(2);
    Check(jobs.IsInitialized() && jobs.GetWorkerThreadCount() == 2, "Worker configuration ignored");
    ExpectThrow<std::logic_error>([&] { jobs.Init(1); }, "Double Init accepted");

    const auto owner = std::this_thread::get_id();
    std::atomic<bool> workerRan{ false };
    auto worker = jobs.Submit([&] {
        Check(std::this_thread::get_id() != owner, "Submitted job did not reach a worker");
        workerRan.store(true, std::memory_order_release);
    });
    // No Wait or other call that helps run tasks: demonstrate actual background execution.
    WaitUntil(workerRan);
    jobs.Wait(worker);
    Check(worker.IsComplete(), "Wait did not complete task");

    const uint32_t count = 10003;
    std::vector<std::atomic<uint32_t>> visits(count);
    for (auto& visit : visits) visit.store(0);
    auto dispatched = jobs.Dispatch(count, [&](uint32_t begin, uint32_t end) {
        Check(begin < end && end <= count, "Invalid Dispatch range");
        for (uint32_t i = begin; i < end; ++i) visits[i].fetch_add(1);
    }, 37);
    jobs.Wait(dispatched);
    for (const auto& visit : visits) Check(visit.load() == 1, "Dispatch missed/duplicated an item");

    auto zero = jobs.Dispatch(0, [](uint32_t, uint32_t) {
        throw std::runtime_error("Zero-sized Dispatch ran its callback");
    });
    Check(zero && zero.IsComplete(), "Empty Dispatch should return a completed handle");
    jobs.Wait(zero);
    ExpectThrow<std::invalid_argument>([&] { jobs.Submit({}); }, "Empty Submit accepted");
    ExpectThrow<std::invalid_argument>([&] { jobs.Dispatch(1, {}); }, "Empty Dispatch accepted");
    ExpectThrow<std::invalid_argument>([&] { jobs.Dispatch(1, [](uint32_t, uint32_t) {}, 0); },
        "Zero Dispatch grain accepted");

    std::atomic<uint32_t> finished{ 0 };
    for (uint32_t i = 0; i < 3000; ++i) jobs.Submit([&] { ++finished; });
    jobs.WaitAll();
    Check(finished == 3000, "Dropping handles lost tasks");

    std::atomic<bool> foreignRejected{ false };
    std::thread foreign([&] {
        try { jobs.Submit([] {}); }
        catch (const std::logic_error&) { foreignRejected = true; }
    });
    foreign.join();
    Check(foreignRejected, "Unregistered external thread accepted");
    jobs.Shutdown();
    Check(!jobs.IsInitialized() && jobs.GetWorkerThreadCount() == 0, "Shutdown left workers running");
    jobs.Wait(worker);
    jobs.Shutdown();
    ExpectThrow<std::logic_error>([&] { jobs.Submit([] {}); }, "Submit after Shutdown accepted");
    jobs.Init(1);
    jobs.Wait(jobs.Submit([] {}));
    jobs.Shutdown();
}

static void TestNestedJobsAndShutdown()
{
    for (uint32_t iteration = 0; iteration < 20; ++iteration)
    {
        JobSystem jobs;
        jobs.Init(1);
        std::atomic<uint32_t> finished{ 0 };
        auto parent = jobs.Submit([&] {
            auto child = jobs.Dispatch(101, [&](uint32_t begin, uint32_t end) {
                finished.fetch_add(end - begin);
            }, 7);
            jobs.Wait(child);
        });
        jobs.Wait(parent);
        Check(finished == 101, "Nested Wait with one worker failed");

        std::atomic<bool> entered{ false }, release{ false };
        auto pending = jobs.Submit([&] {
            entered.store(true, std::memory_order_release);
            WaitUntil(release);
            // Shutdown has begun by the time release is set. Child tasks must
            // still be accepted so already submitted work can finish normally.
            auto child = jobs.Submit([&] { ++finished; });
            jobs.Wait(child);
        });
        WaitUntil(entered);
        std::thread releaser([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            release.store(true, std::memory_order_release);
        });
        jobs.Shutdown();
        releaser.join();
        Check(pending.IsComplete() && finished == 102, "Shutdown lost active/child work");
    }

    std::atomic<uint32_t> destructed{ 0 };
    JobSystem::TaskHandle survivor;
    {
        JobSystem jobs;
        jobs.Init(2);
        for (uint32_t i = 0; i < 1000; ++i)
            survivor = jobs.Submit([&] { ++destructed; });
    }
    Check(destructed == 1000 && survivor.IsComplete(), "Destructor did not drain discarded handles");
}

static void TestExceptions()
{
    JobSystem jobs;
    jobs.Init(2);
    auto failed = jobs.Submit([] { throw std::runtime_error("Callback failure"); });
    ExpectThrow<std::runtime_error>([&] { jobs.Wait(failed); }, "Wait lost callback exception");
    jobs.WaitAll(); // The previous exception has already been observed by Wait.

    auto forbidden = jobs.Submit([&] { jobs.WaitAll(); });
    ExpectThrow<std::logic_error>([&] { jobs.Wait(forbidden); }, "WaitAll inside a job accepted");

    // Failures from discarded handles survive collection until WaitAll.
    jobs.Submit([] { throw std::runtime_error("Unobserved failure"); });
    std::atomic<uint32_t> completed{ 0 };
    for (uint32_t i = 0; i < 1000; ++i) jobs.Submit([&] { ++completed; });
    ExpectThrow<std::runtime_error>([&] { jobs.WaitAll(); }, "WaitAll lost discarded task exception");
    Check(completed == 1000, "WaitAll reported failure before draining other work");
    jobs.WaitAll();

    auto lastFailure = jobs.Submit([] { throw std::runtime_error("Shutdown failure"); });
    jobs.Shutdown();
    Check(lastFailure.IsComplete(), "Failed task not drained at shutdown");
    ExpectThrow<std::runtime_error>([&] { jobs.Wait(lastFailure); }, "Shutdown lost retained exception");
    jobs.Init(1);
    std::atomic<bool> alive{ false };
    jobs.Wait(jobs.Submit([&] { alive = true; }));
    Check(alive, "Scheduler did not recover after callback exception and reinitialization");

    JobSystem other;
    other.Init(1);
    auto foreign = other.Submit([] {});
    ExpectThrow<std::invalid_argument>([&] { jobs.Wait(foreign); }, "Wait accepted another scheduler's task");
    other.Wait(foreign);
}

int main()
{
    try
    {
        TestSubmitAndDispatch();
        TestNestedJobsAndShutdown();
        TestExceptions();
        std::cout << "PASS: background Submit, Dispatch coverage, lifetime, nested Wait, graceful Shutdown, reinit, errors\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
