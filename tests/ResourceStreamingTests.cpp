#include "ResourceManager.h"
#include "JobSystem.h"
#include "D3DRenderAdapter.h"
#include <d3d12sdklayers.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    void Write(const std::filesystem::path& path, const std::string& data)
    {
        std::ofstream file(path, std::ios::binary);
        file.write(data.data(), data.size());
        Check(file.good(), "Could not write streaming fixture");
    }
    struct ReleaseOnExit
    {
        JobSystem& jobs;
        std::atomic<bool>& released;
        ~ReleaseOnExit()
        {
            released.store(true, std::memory_order_release);
            // Join before the captured atomics leave scope, also on failure.
            jobs.WaitAll();
        }
    };
    void CheckDiagnostics(ID3D12InfoQueue* diagnostics)
    {
        if (!diagnostics) return;
        for (UINT64 i = 0; i < diagnostics->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0;
            diagnostics->GetMessage(i, nullptr, &size);
            std::vector<uint8_t> bytes(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            diagnostics->GetMessage(i, message, &size);
            Check(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION, message->pDescription);
        }
    }
}

void RunResourceStreamingTests(const std::filesystem::path& repo, const std::filesystem::path& fixtures)
{
    using namespace std::chrono_literals;
    namespace fs = std::filesystem;
    std::string tga(18, '\0');
    tga[2] = 2; tga[12] = 1; tga[14] = 1; tga[16] = 32; tga[17] = 0x28;
    tga.append("\x00\x00\xff\xff", 4);
    Write(fixtures / "async.tga", tga);
    fs::copy_file(repo / "Textures/white1x1.dds", fixtures / "async.dds", fs::copy_options::overwrite_existing);
    Write(fixtures / "async-broken.dds", "not a DDS");
    Write(fixtures / "async.mtl", "newmtl surface\nmap_Kd async.tga\n");
    Write(fixtures / "async.obj", "mtllib async.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\nusemtl surface\nf 1/1 2/2 3/3\n");

    JobSystem jobs;
    jobs.Init(2);
    std::atomic<bool> release{ false };
    std::atomic<uint32_t> entered{ 0 };
    ReleaseOnExit unblock{ jobs, release };
    std::vector<JobSystem::TaskHandle> blockers;
    for (int i = 0; i < 2; ++i)
        blockers.push_back(jobs.Submit([&]
        {
            entered.fetch_add(1, std::memory_order_release);
            while (!release.load(std::memory_order_acquire)) std::this_thread::sleep_for(1ms);
        }));
    const auto gateDeadline = std::chrono::steady_clock::now() + 5s;
    while (entered.load(std::memory_order_acquire) != 2 && std::chrono::steady_clock::now() < gateDeadline)
        std::this_thread::sleep_for(1ms);
    Check(entered.load() == 2, "Both worker threads did not start");

    ResourceManager resources;
    resources.InitLoading(jobs, 2);
    Check(resources.GetTexture(resources.GetPlaceholderTexture()).imageData.pixels.size() == 16,
        "Checkerboard must exist before any jobs");
    std::vector<TextureID> ids;
    ids.push_back(resources.LoadTexture((fixtures / "async.tga").wstring()));
    ids.push_back(resources.LoadTexture((fixtures / "async.dds").wstring()));
    const TextureID failed = resources.LoadTexture((fixtures / "async-broken.dds").wstring());
    Check(resources.LoadTexture((fixtures / "./async.tga").wstring()) == ids[0], "Pending requests were not deduplicated");
    for (int i = 0; i < 10; ++i)
    {
        const auto path = fixtures / ("async-copy-" + std::to_string(i) + ".tga");
        Write(path, tga);
        ids.push_back(resources.LoadTexture(path.wstring()));
    }
    Material mat;
    mat.name = "Streaming material"; mat.albedo = ids[0]; mat.normal = ids[1];
    const MaterialID material = resources.CreateMaterial(mat);
    Material brokenMat = mat;
    brokenMat.albedo = failed;
    const MaterialID brokenMaterial = resources.CreateMaterial(brokenMat);
    const MeshID cube = resources.CreateCube(material);
    const MeshID plane = resources.CreatePlane(material);
    const MeshID sphere = resources.CreateSphere(material, 8, 4);
    const MeshID cubeCopy = resources.CreateCube(brokenMaterial);
    const MeshID planeCopy = resources.CreatePlane(brokenMaterial);
    const MeshID sphereCopy = resources.CreateSphere(brokenMaterial, 8, 4);
    const MeshID differentSphere = resources.CreateSphere(material, 8, 5);
    const MeshID minimumSphere = resources.CreateSphere(material, 3, 2);
    const MeshID clampedSphere = resources.CreateSphere(material, 0, 0);
    Check(cube != cubeCopy && resources.GetMesh(cube).sharedGeometry == resources.GetMesh(cubeCopy).sharedGeometry &&
        resources.GetMesh(plane).sharedGeometry == resources.GetMesh(planeCopy).sharedGeometry &&
        resources.GetMesh(sphere).sharedGeometry == resources.GetMesh(sphereCopy).sharedGeometry,
        "Identical primitives did not share CPU geometry independently of material");
    Check(resources.GetMesh(sphere).sharedGeometry != resources.GetMesh(differentSphere).sharedGeometry &&
        resources.GetMesh(minimumSphere).sharedGeometry == resources.GetMesh(clampedSphere).sharedGeometry,
        "Sphere cache ignored tessellation or did not normalize minimum tessellation");
    const MeshID model = resources.LoadMesh((fixtures / "async.obj").string());
    resources.SetMeshMaterial(model, material);
    const MaterialID overrideMaterial = resources.CreateSolidMaterial("Deferred override", glm::vec3(0.5f));
    resources.SetSubmeshMaterial(model, 0, overrideMaterial);
    const MeshID badModel = resources.LoadMesh((fixtures / "missing.obj").string());
    for (auto id : ids)
        Check(resources.GetTexture(id).state == ResourceState::Loading && resources.GetTexture(id).imageData.pixels.empty(),
            "Async request decoded on the caller or published before PumpLoading");
    Check(resources.GetMesh(model).state == ResourceState::Loading, "Model import was synchronous");

    WNDCLASS wc = {};
    wc.hInstance = GetModuleHandle(nullptr); wc.lpfnWndProc = DefWindowProc;
    wc.lpszClassName = L"ResourceStreamingTest";
    Check(RegisterClass(&wc) != 0, "Could not register streaming window");
    HWND window = CreateWindow(wc.lpszClassName, L"Streaming test", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    Check(window != nullptr, "Could not create hidden streaming window");
    {
        D3DRenderAdapter renderer;
        renderer.SetResourceManager(&resources);
        renderer.Init(window, 64, 64);
        D3DRenderAdapter::UploadBudget budget;
        budget.maxBytes = 4; // One tiny texture, or one oversized resource, per frame.
        budget.maxMilliseconds = 1000.0; // Count limit is deterministic in this test.
        renderer.SetUploadBudget(budget);
        Check(renderer.mGeometries.empty(), "Primitive creation eagerly uploaded GPU buffers");
        const auto bindings = renderer.GetImGuiBindings();
        ComPtr<ID3D12InfoQueue> diagnostics;
        bindings.Device->QueryInterface(IID_PPV_ARGS(&diagnostics));
        if (diagnostics) diagnostics->ClearStoredMessages();
        const TransformComponent object{ glm::vec3(0), glm::vec3(0), glm::vec3(1) };
        const TransformComponent cameraTransform{ glm::vec3(0, 0, -3), glm::vec3(0), glm::vec3(1) };
        CameraComponent camera{};
        camera.fov = 1.0f; camera.nearZ = 0.1f; camera.farZ = 100.0f;
        const auto draw = [&]
        {
            renderer.SetCamera(camera, cameraTransform);
            renderer.SetLights(SceneLightData{});
            renderer.UpdCB();
            renderer.SetTransform(object);
            renderer.DrawMesh(cube);
            renderer.SetTransform(object);
            renderer.DrawMesh(plane);
            renderer.SetTransform(object);
            renderer.DrawMesh(sphere);
            for (const MeshID copy : { cubeCopy, planeCopy, sphereCopy })
            {
                renderer.SetTransform(object);
                renderer.DrawMesh(copy);
            }
            renderer.SetTransform(object);
            renderer.DrawMesh(model);
        };
        MaterialGPU* cachedMaterial = nullptr;
        int placeholder = -1;
        for (int frame = 0; frame < 3; ++frame)
        {
            renderer.BeginFrame();
            Check(resources.GetActiveLoadCount() <= 2 && resources.GetQueuedLoadCount() > 0,
                "Resource job admission was not bounded");
            cachedMaterial = renderer.GetOrLoadMaterial(material);
            placeholder = renderer.mTextures.at(std::to_string(resources.GetPlaceholderTexture()))->SrvHeapIndex;
            Check(cachedMaterial->DiffuseSrvHeapIndex == placeholder &&
                cachedMaterial->NormalSrvHeapIndex == renderer.mTextures.at(std::to_string(resources.GetFlatNormalTexture()))->SrvHeapIndex,
                "Pending textures did not bind checkerboard and flat normal");
            Check(resources.GetTexture(ids[0]).state == ResourceState::Loading,
                "Frame pump waited for busy workers or decoded on the render thread");
            draw();
            for (const MeshID primitive : { cube, plane, sphere })
            {
                Check(renderer.mGeometries.contains(primitive) && resources.GetMesh(primitive).state == ResourceState::Ready,
                    "Primitive missed its first draw while textures were still loading");
                MeshGPU* gpu = renderer.mGeometries.at(primitive).get();
                Check(renderer.GetMeshGPU(primitive) == gpu,
                    "Primitive did not reuse its cached GPU buffers");
            }
            Check(!renderer.mGeometries.contains(model), "Loading model was uploaded before CPU import finished");
            for (const auto& pair : { std::pair{cube, cubeCopy}, std::pair{plane, planeCopy}, std::pair{sphere, sphereCopy} })
            {
                auto* first = renderer.GetMeshGPU(pair.first);
                auto* second = renderer.GetMeshGPU(pair.second);
                Check(first != second && first->vertexBuffer == second->vertexBuffer && first->indexBuffer == second->indexBuffer,
                    "Identical primitives allocated separate GPU geometry buffers");
                Check(first->submeshes[0].material == material && second->submeshes[0].material == brokenMaterial,
                    "Shared primitive geometry coupled per-mesh materials");
            }
            Check(!renderer.mGeometries.contains(differentSphere), "Unused primitive eagerly uploaded buffers");
            renderer.EndFrame();
        }
        release.store(true, std::memory_order_release);
        const auto continuousDeadline = std::chrono::steady_clock::now() + 5s;
        while (resources.GetLoadingStats().completedResults < ids.size() + 3 && std::chrono::steady_clock::now() < continuousDeadline)
            std::this_thread::sleep_for(1ms);
        Check(resources.GetLoadingStats().completedResults >= ids.size() + 3 && resources.GetQueuedLoadCount() == 0,
            "CPU consumers waited for a frame to start the next request");
        Check(resources.GetTexture(ids[0]).state == ResourceState::Loading,
            "Worker published directly into resource maps");
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        size_t frames = 0;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const size_t beforeTextures = renderer.mTextures.size();
            const size_t beforeMeshes = renderer.mGeometries.size();
            renderer.BeginFrame();
            Check(renderer.mTextures.size() <= beforeTextures + 1 && renderer.mGeometries.size() <= beforeMeshes + 1,
                "GPU upload pump exceeded its per-frame count budget");
            draw();
            Check(renderer.GetOrLoadMaterial(material) == cachedMaterial, "Material cache pointer changed during streaming");
            renderer.EndFrame();
            ++frames;
            if (!resources.HasPendingLoads() && resources.PeekTextureUpload() == 0 && renderer.mGeometries.contains(model)) break;
            std::this_thread::sleep_for(1ms);
        }
        Check(!resources.HasPendingLoads() && resources.PeekTextureUpload() == 0, "Streaming did not finish");
        Check(resources.GetLoadingStats().readyBytes == 0, "Completed GPU resources retained their pending-memory charge");
        Check(frames >= ids.size(), "Texture budget did not distribute uploads across frames");
        Check(cachedMaterial->DiffuseSrvHeapIndex != placeholder &&
            cachedMaterial->DiffuseSrvHeapIndex == renderer.mTextures.at(std::to_string(ids[0]))->SrvHeapIndex &&
            cachedMaterial->NormalSrvHeapIndex == renderer.mTextures.at(std::to_string(ids[1]))->SrvHeapIndex,
            "Cached material did not switch to the uploaded texture");
        Check(resources.GetTexture(failed).state == ResourceState::Failed && !resources.GetTexture(failed).error.empty(),
            "Decode failure was not recorded");
        Check(resources.GetMesh(badModel).state == ResourceState::Failed && !resources.GetMesh(badModel).error.empty(),
            "Model failure was not recorded");
        Check(resources.GetMesh(model).submeshes.at(0).material == overrideMaterial && renderer.mGeometries.contains(model),
            "Deferred model material override or GPU finalization failed");
        renderer.BeginFrame();
        Check(renderer.GetOrLoadMaterial(brokenMaterial)->DiffuseSrvHeapIndex == placeholder,
            "Failed texture did not retain the checkerboard");
        draw();
        renderer.EndFrame();
        renderer.FlushCommandQueue();
        renderer.CleanupMeshUploadBuffers();
        resources.SetMeshMaterial(cubeCopy, overrideMaterial);
        renderer.BeginFrame();
        Check(renderer.GetMeshGPU(cubeCopy)->submeshes[0].material == overrideMaterial &&
            renderer.GetMeshGPU(cube)->submeshes[0].material == material,
            "Editing a material affected another primitive sharing geometry");
        renderer.EndFrame();
        renderer.FlushCommandQueue();
        renderer.CleanupMeshUploadBuffers();
        for (const auto& [key, texture] : renderer.mTextures)
            Check(!texture->UploadHeap && texture->uploadCompleteFence == 0, "Completed texture upload heap was retained");
        CheckDiagnostics(diagnostics.Get());

        // Pause frame pumping: consumers must fill the byte budget, then stop producing.
        ResourceManager limited;
        limited.InitLoading(jobs, 2);
        limited.SetLoadingMemoryBudget(1024);
        std::vector<TextureID> limitedTextures;
        for (int i = 0; i < 128; ++i)
        {
            const auto path = fixtures / ("memory-" + std::to_string(i) + ".tga");
            Write(path, tga);
            limitedTextures.push_back(limited.LoadTexture(path.wstring()));
        }
        const auto memoryDeadline = std::chrono::steady_clock::now() + 5s;
        while (limited.GetLoadingStats().readyBytes < 1024 && std::chrono::steady_clock::now() < memoryDeadline)
            std::this_thread::sleep_for(1ms);
        std::this_thread::sleep_for(20ms);
        const auto full = limited.GetLoadingStats();
        Check(full.readyBytes >= 1024 && full.readyBytes <= 1024 + 2 * (4 + sizeof(ImageSubresource)) &&
            full.completedResults > 4 && limited.GetQueuedLoadCount() > 0,
            "Worker results were unaccounted, production was count-limited, or byte backpressure failed");
        const auto queuedWhenFull = limited.GetQueuedLoadCount();
        std::this_thread::sleep_for(20ms);
        Check(limited.GetQueuedLoadCount() == queuedWhenFull, "Full ready-data budget did not pause CPU production");
        renderer.SetResourceManager(&limited);
        budget.maxBytes = 1024 * 1024;
        renderer.SetUploadBudget(budget);
        bool uploadedMoreThanFour = false;
        while (std::chrono::steady_clock::now() < memoryDeadline)
        {
            const size_t before = renderer.mTextures.size();
            renderer.BeginFrame();
            uploadedMoreThanFour |= renderer.mTextures.size() > before + 4;
            renderer.EndFrame();
            if (!limited.HasPendingLoads() && limited.PeekTextureUpload() == 0) break;
        }
        Check(uploadedMoreThanFour && limited.GetTextureProgress().ready == limitedTextures.size() &&
            limited.GetLoadingStats().readyBytes == 0,
            "GPU pump kept an operation-count limit or did not resume memory-blocked consumers");
        std::vector<MeshID> limitedModels;
        for (int i = 0; i < 6; ++i) limitedModels.push_back(limited.LoadMesh((fixtures / "async.obj").string()));
        const auto modelDeadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < modelDeadline)
        {
            limited.PumpLoading();
            size_t modelBytes = 0;
            for (auto id : limitedModels)
                if (limited.GetMesh(id).state == ResourceState::CpuReady) modelBytes += limited.GetMesh(id).GeometryBytes();
            Check(limited.GetLoadingStats().readyBytes >= modelBytes, "Pending model geometry escaped memory accounting");
            renderer.BeginFrame();
            for (auto id : limitedModels) renderer.GetMeshGPU(id);
            renderer.EndFrame();
            bool ready = true;
            for (auto id : limitedModels) ready &= limited.GetMesh(id).state == ResourceState::Ready;
            if (ready && !limited.HasPendingLoads() && limited.PeekTextureUpload() == 0) break;
        }
        for (auto id : limitedModels) Check(limited.GetMesh(id).state == ResourceState::Ready, "Memory-blocked model did not finish");
        Check(limited.GetLoadingStats().readyBytes == 0, "Model GPU upload did not release pending memory");
        renderer.FlushCommandQueue();
        limited.SetLoadingMemoryBudget(1);
        std::vector<TextureID> cancelAtCapacity;
        for (int i = 0; i < 8; ++i)
        {
            const auto path = fixtures / ("full-cancel-" + std::to_string(i) + ".tga");
            Write(path, tga);
            cancelAtCapacity.push_back(limited.LoadTexture(path.wstring()));
        }
        const auto capacityDeadline = std::chrono::steady_clock::now() + 5s;
        while (limited.GetLoadingStats().readyBytes < 1 && std::chrono::steady_clock::now() < capacityDeadline)
            std::this_thread::sleep_for(1ms);
        Check(limited.GetLoadingStats().readyBytes >= 1 && limited.GetQueuedLoadCount() > 0,
            "Shutdown-at-capacity fixture did not fill pending memory");
        const auto otherWorkFinished = std::make_shared<std::atomic<bool>>(false);
        const auto otherWork = jobs.Submit([otherWorkFinished] { otherWorkFinished->store(true, std::memory_order_release); });
        while (!otherWork.IsComplete() && std::chrono::steady_clock::now() < capacityDeadline)
            std::this_thread::sleep_for(1ms);
        Check(otherWork.IsComplete() && otherWorkFinished->load(std::memory_order_acquire),
            "Memory-blocked resource consumers monopolized scheduler workers");
        jobs.Wait(otherWork);
        limited.ShutdownLoading();
        for (auto id : cancelAtCapacity)
            Check(limited.GetTexture(id).state == ResourceState::Failed, "Shutdown did not cancel a memory-blocked request");
        renderer.SetResourceManager(&resources);
        CheckDiagnostics(diagnostics.Get());
    }
    DestroyWindow(window);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    resources.ShutdownLoading();
    {
        ResourceManager cancelling;
        cancelling.InitLoading(jobs, 2);
        std::vector<TextureID> pending;
        for (int i = 0; i < 12; ++i)
            pending.push_back(cancelling.LoadTexture((fixtures / ("cancel-" + std::to_string(i) + ".tga")).wstring()));
        cancelling.PumpLoading();
        cancelling.ShutdownLoading();
        Check(!cancelling.HasPendingLoads(), "Shutdown did not drain submitted work and cancel queued work");
        for (auto id : pending) Check(cancelling.GetTexture(id).state == ResourceState::Failed, "Cancelled resource remained Loading");
        bool rejected = false;
        try { cancelling.LoadTexture((fixtures / "after-shutdown.tga").wstring()); }
        catch (const std::logic_error&) { rejected = true; }
        Check(rejected, "Shutdown admitted a new resource request");
    }
    jobs.Shutdown();
    std::cout << "PASS: continuous CPU consumers; byte backpressure including models; shared CPU/GPU primitives and independent materials; frames with occupied workers; checkerboard switch; byte/time GPU pump; errors; deferred overrides; fence cleanup; shutdown at capacity\n";
}
