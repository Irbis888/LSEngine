#include "PhysicsSystem.h"
#include "ResourceManager.h"
#include "SceneSerializer.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;
using Scene = SceneSerializer::SceneData;

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void Populate(entt::registry& reg, const Scene& scene)
{
    for (const auto& item : scene.entities)
    {
        auto entity = reg.create();
        reg.emplace<TransformComponent>(entity, item.transform);
        if (item.rigidbody) reg.emplace<RigidbodyComponent>(entity, *item.rigidbody);
        if (item.collider) reg.emplace<ColliderComponent>(entity, *item.collider);
    }
}

static Scene Isolated(size_t count)
{
    Scene scene;
    for (size_t i = 0; i < count; ++i)
    {
        SceneSerializer::EntityDescription item;
        item.transform = { { float(i % 128) * 4, float(i / 128) * 4, 0 }, {}, { 1, -1, 1 } };
        item.rigidbody = RigidbodyComponent{};
        item.rigidbody->useGravity = false;
        item.rigidbody->velocity = { 0.01f, -0.02f, 0.03f };
        item.rigidbody->acceleration = { 0.001f, 0.002f, -0.003f };
        item.collider = ColliderComponent{};
        if (i % 2) item.collider->type = ColliderType::Sphere;
        if (i % 17 == 0) item.rigidbody->type = RigidbodyType::Kinematic;
        scene.entities.push_back(item);
    }
    return scene;
}

static void Compare(JobSystem& jobs, const Scene& scene)
{
    entt::registry serial, parallel;
    Populate(serial, scene);
    Populate(parallel, scene);
    PhysicsSystem a(jobs), b(jobs);
    a.SetSchedulingSettings({ false, 128, 64 });
    b.SetSchedulingSettings({ true, 1, 37 });
    GameTimer timer;
    FrameContext context{ timer, {}, 1.0f / 60 };
    for (int step = 0; step < 600; ++step)
    {
        // Mutations happen only between steps. Exercise storage relocation,
        // removal and reuse, including different body/collider memberships.
        if (step == 100)
        {
            for (auto* reg : { &serial, &parallel })
            {
                auto e = reg->create();
                reg->emplace<TransformComponent>(e, glm::vec3(1000), glm::vec3(0), glm::vec3(1));
                reg->emplace<RigidbodyComponent>(e).useGravity = false;
                auto colliderOnly = reg->create();
                reg->emplace<TransformComponent>(colliderOnly, glm::vec3(2000), glm::vec3(0), glm::vec3(1));
                reg->emplace<ColliderComponent>(colliderOnly);
                reg->destroy(static_cast<entt::entity>(0));
            }
        }
        a.Update(serial, context);
        const auto serialStats = PhysicsStats::BroadPhase();
        const auto collisions = PhysicsStats::GetFrameCollisionCount();
        b.Update(parallel, context);
        Check(collisions == PhysicsStats::GetFrameCollisionCount(), "Parallel collision count differs");
        const auto& parallelStats = PhysicsStats::BroadPhase();
        Check(serialStats.narrowPhaseTests == parallelStats.narrowPhaseTests &&
            serialStats.aabbTests == parallelStats.aabbTests, "Parallel solver changed pair queries/order");
        for (auto entity : serial.view<TransformComponent>())
        {
            const auto& ta = serial.get<TransformComponent>(entity);
            const auto& tb = parallel.get<TransformComponent>(entity);
            Check(ta.position == tb.position && ta.scale == tb.scale && ta.rotation == tb.rotation,
                "Serial/parallel transforms differ (exact comparison)");
            if (auto* body = serial.try_get<RigidbodyComponent>(entity))
                Check(body->velocity == parallel.get<RigidbodyComponent>(entity).velocity,
                    "Serial/parallel velocities differ (exact comparison)");
        }
        Check(b.GetTimings().parallelIntegration && b.GetTimings().parallelAabb,
            "Parallel comparison accidentally used the serial fallback");
    }
}

static double Quantile(std::vector<double> values, double fraction)
{
    std::sort(values.begin(), values.end());
    return values.empty() ? 0 : values[size_t((values.size() - 1) * fraction)];
}

static void Benchmark(JobSystem& jobs, const Scene& scene, const std::string& name,
    bool parallel, uint32_t range, bool loading, int repeat, const fs::path& repo,
    const fs::path& modelPath, std::ofstream& csv)
{
    entt::registry reg;
    Populate(reg, scene);
    PhysicsSystem physics(jobs);
    // Force each mode to measure the crossover, independently of the default threshold.
    physics.SetSchedulingSettings({ parallel, 1, range });
    GameTimer timer;
    FrameContext context{ timer, {}, 1.0f / 60 };
    for (int step = 0; step < 20; ++step) physics.Update(reg, context);
    // Start both measurements from the same state after allocation/cache warmup.
    reg.clear();
    Populate(reg, scene);
    ResourceManager resources;
    std::vector<MeshID> models;
    if (loading)
    {
        resources.InitLoading(jobs, 4);
        for (int i = 0; i < 32; ++i) models.push_back(resources.LoadMesh(modelPath.string()));
        for (const auto& file : fs::directory_iterator(repo / "Textures/StreamingStress"))
            if (file.path().extension() == ".dds") resources.LoadTexture(file.path().wstring());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!resources.GetActiveLoadCount() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        Check(resources.GetActiveLoadCount() > 0, "No real background decoder started");
    }
    std::vector<double> total, overlapTotal;
    PhysicsStats::StepTimings sum;
    size_t maxActive = 0;
    const int steps = scene.entities.size() > 4096 ? 120 : 600;
    for (int step = 0; step < steps; ++step)
    {
        const auto active = resources.GetActiveLoadCount();
        maxActive = (std::max)(maxActive, active);
        physics.Update(reg, context);
        const auto& t = physics.GetTimings();
        total.push_back(t.totalMs);
        if (active) overlapTotal.push_back(t.totalMs);
        sum.prepareMs += t.prepareMs;
        sum.integrateMs += t.integrateMs;
        sum.aabbMs += t.aabbMs;
        sum.broadPhaseMs += t.broadPhaseMs;
        sum.solverMs += t.solverMs;
        sum.totalMs += t.totalMs;
        if (loading)
        {
            // CPU benchmark only: publication/releasing staged bytes is outside
            // the timed physics step. No fake GPU timing is reported.
            resources.PumpLoading(1);
            while (auto id = resources.PeekTextureUpload()) resources.FinishTextureUpload(id);
            for (auto id : models)
                if (resources.GetMesh(id).state == ResourceState::CpuReady)
                {
                    resources.FinishMeshUpload(id);
                    // This harness has no GPU/scene references to imported geometry.
                    // Discard it after consuming the result to keep a continuous
                    // stream bounded, independently of the production cache policy.
                    auto& mesh = resources.GetMesh(id);
                    std::vector<Vertex>().swap(mesh.vertices);
                    std::vector<uint32_t>().swap(mesh.indices);
                }
            // Keep real imports active throughout the timed run, including the
            // large scenes; a one-shot texture queue finishes too soon there.
            while (resources.GetQueuedLoadCount() < 8)
                models.push_back(resources.LoadMesh(modelPath.string()));
        }
    }
    Check(!loading || (!overlapTotal.empty() && maxActive <= jobs.GetWorkerThreadCount() - 1),
        "Loader consumed reserved capacity or no measured step overlapped loading");
    csv << name << ',' << scene.entities.size() << ',' << parallel << ',' << range << ',' << loading << ',' << repeat
        << ',' << sum.prepareMs / steps << ',' << sum.integrateMs / steps << ',' << sum.aabbMs / steps
        << ',' << sum.broadPhaseMs / steps << ',' << sum.solverMs / steps << ',' << sum.totalMs / steps
        << ',' << Quantile(total, 0.5) << ',' << Quantile(total, 0.95) << ',' << Quantile(total, 1)
        << ',' << overlapTotal.size() << ',' << Quantile(overlapTotal, 0.5) << ',' << maxActive << '\n';
    csv.flush();
    std::cout << name << " parallel=" << parallel << " range=" << range << " loading=" << loading
        << " median_ms=" << Quantile(total, 0.5) << " p95_ms=" << Quantile(total, 0.95)
        << " integrate_aabb_mean_ms=" << (sum.integrateMs + sum.aabbMs) / steps
        << " overlap_steps=" << overlapTotal.size() << '\n';
    resources.ShutdownLoading();
    jobs.WaitAll();
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 3, "Expected repo and output directory");
        const fs::path repo = argv[1], output = argv[2];
        const auto project = repo / "Chapter 9 Texturing/Try2";
        const auto stress = SceneSerializer::Read((project / "Scenes/PhysicsStress256.json").string());
        const auto mixed = SceneSerializer::Read((project / "Scenes/PhysicsMixed512.json").string());
        JobSystem jobs;
        jobs.Init(4);
        Compare(jobs, stress);
        Compare(jobs, mixed);
        Compare(jobs, Isolated(257));
        std::cout << "PASS: 600 exact serial/parallel steps per scene, mixed components, registry mutation between steps\n";
        PhysicsSystem emptySystem(jobs);
        entt::registry empty;
        GameTimer timer;
        FrameContext context{ timer, {}, 1.0f / 60 };
        emptySystem.Update(empty, context);
        Check(!emptySystem.GetTimings().parallelIntegration && !emptySystem.GetTimings().parallelAabb, "Empty step was dispatched");
        bool rejected = false;
        try { emptySystem.SetSchedulingSettings({ true, 0, 64 }); }
        catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "Invalid parallel threshold accepted");
        Populate(empty, Isolated(127));
        emptySystem.SetSchedulingSettings({ true, 128, 64 });
        emptySystem.Update(empty, context);
        Check(!emptySystem.GetTimings().parallelIntegration && !emptySystem.GetTimings().parallelAabb,
            "Small nonempty scene did not use serial path");
        fs::create_directories(output);
        const auto modelPath = output / "background.obj";
        {
            std::ofstream model(modelPath);
            for (int i = 0; i < 20000; ++i)
                model << "v " << i * 2 << " 0 0\nv " << i * 2 + 1 << " 0 0\nv " << i * 2 << " 1 0\n";
            for (int i = 0; i < 20000; ++i)
                model << "f " << i * 3 + 1 << ' ' << i * 3 + 2 << ' ' << i * 3 + 3 << '\n';
        }
        // Warm the same file set once; no cache flush and no compiler running during timings.
        for (const auto& file : fs::directory_iterator(repo / "Textures/StreamingStress"))
        {
            std::ifstream stream(file.path(), std::ios::binary);
            std::string warm((std::istreambuf_iterator<char>(stream)), {});
        }
        std::ofstream csv(output / "results.csv");
        csv << std::setprecision(9);
        csv << "scene,count,parallel,range,loading,repeat,prepare_ms,integrate_ms,aabb_ms,broad_phase_ms,solver_ms,mean_ms,median_ms,p95_ms,max_ms,overlap_steps,overlap_median_ms,max_active_loads\n";
        const std::vector<std::pair<std::string, Scene>> scenes = {
            { "PhysicsStress256", stress }, { "PhysicsMixed512", mixed },
            { "Isolated8192", Isolated(8192) }, { "Isolated32768", Isolated(32768) }
        };
        for (const auto& [name, scene] : scenes)
            for (int repeat = 1; repeat <= 3; ++repeat)
                for (bool loading : { false, true })
                    // Alternate order to reduce systematic warmup/thermal bias.
                    for (bool first : { false, true })
                        Benchmark(jobs, scene, name, first != (repeat % 2 == 0), 64,
                            loading, repeat, repo, modelPath, csv);
        for (uint32_t range : { 128u, 256u, 512u })
            Benchmark(jobs, scenes[2].second, "Isolated8192", true, range, false, 1, repo, modelPath, csv);
        for (size_t count : { 1024u, 2048u, 4096u })
            for (bool parallel : { false, true })
                Benchmark(jobs, Isolated(count), "Crossover" + std::to_string(count), parallel,
                    64, false, 1, repo, modelPath, csv);
        std::cout << "PASS: physics stage measurements with real DDS decoding/Assimp imports; loader reserves a worker\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
