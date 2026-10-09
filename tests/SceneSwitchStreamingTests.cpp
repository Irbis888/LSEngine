#include "Engine.h"
#include "D3DRenderAdapter.h"
#include <d3d12sdklayers.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

// Only editor input/physics policy is stubbed; the test uses real Engine,
// SceneSerializer, ResourceManager, JobSystem, RenderSystem and D3D12.
namespace ImGuiBridge { bool WantsCaptureInput() { return false; } }
bool Editor_IsPhysicsEnabled() { return false; }

namespace
{
    void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    size_t Meshes(const entt::registry& registry)
    {
        size_t count = 0;
        for (auto entity : registry.view<MeshComponent>()) { (void)entity; ++count; }
        return count;
    }
}

void RunSceneSwitchStreamingTests(const std::filesystem::path& repo, const std::filesystem::path& fixtures)
{
    using namespace std::chrono_literals;
    namespace fs = std::filesystem;
    using nlohmann::json;
    const auto startup = fixtures / "scene-switch";
    fs::create_directories(startup / "Scenes");
    json scene{ { "entities", json::array({
        json{ { "tag", "OldCube" }, { "mesh", {
            { "source", "primitive" }, { "primitive", "cube" }, { "material", {
                { "albedo", (fixtures / "async.tga").generic_string() } } } } } },
        json{ { "tag", "Camera" }, { "transform", { { "position", { 0, 0, -3 } } } },
            { "camera", { { "fov", 1.0 }, { "nearZ", 0.1 }, { "farZ", 1500 } } } }
    }) } };
    std::ofstream(startup / "Scenes/DemoScene.json") << scene.dump();
    const HINSTANCE instance = GetModuleHandle(nullptr);
    WNDCLASS wc = {}; wc.hInstance = instance; wc.lpfnWndProc = DefWindowProc; wc.lpszClassName = L"SceneSwitchStreamingTest";
    Check(RegisterClass(&wc) != 0, "Could not register scene switch window");
    HWND window = CreateWindow(wc.lpszClassName, L"Scene switch test", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, instance, nullptr);
    Check(window != nullptr, "Could not create scene switch window");
    {
        D3DRenderAdapter renderer;
        renderer.Init(window, 64, 64);
        ComPtr<ID3D12InfoQueue> diagnostics;
        renderer.GetImGuiBindings().Device->QueryInterface(IID_PPV_ARGS(&diagnostics));
        if (diagnostics) diagnostics->ClearStoredMessages();
        GameTimer timer; timer.Reset();
        InputState input{};
        FrameContext context{ timer, input, 0.0f };
        Engine engine(&renderer);
        const auto previousDirectory = fs::current_path();
        fs::current_path(startup);
        try { engine.Init(timer); }
        catch (...) { fs::current_path(previousDirectory); throw; }
        fs::current_path(previousDirectory);
        const auto frame = [&]
        {
            timer.Tick();
            renderer.BeginFrame();
            engine.Update(context);
            engine.Draw(context);
            renderer.EndFrame();
        };
        const auto startupDeadline = std::chrono::steady_clock::now() + 10s;
        while ((engine.IsSceneLoading() || engine.GetResources().GetTextureProgress().ready < 1 || renderer.mGeometries.empty()) &&
            std::chrono::steady_clock::now() < startupDeadline)
        { frame(); std::this_thread::sleep_for(1ms); }
        Check(!engine.IsSceneLoading() && Meshes(engine.GetRegistry()) == 1 && !renderer.mGeometries.empty(),
            "Async startup scene did not finish");
        std::string error;
        const auto requestStart = std::chrono::steady_clock::now();
        Check(engine.LoadScene((repo / "Chapter 9 Texturing/Try2/Scenes/TextureStreaming1000.json").string(), error), "Scene request rejected");
        const double requestMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - requestStart).count();
        Check(requestMs < 50.0, "Scene request still performs blocking parsing/publication");
        Check(engine.IsSceneLoading() && Meshes(engine.GetRegistry()) == 1, "Scene request immediately discarded the rendered scene");
        Check(!engine.SaveScene((startup / "partial.json").string(), error), "Saving a partial scene was allowed");
        Check(!engine.LoadScene("missing.json", error), "Concurrent scene requests were accepted");
        size_t previousCreated = 0, frames = 0;
        bool sawPlaceholder = false, sawPartialTextures = false;
        double maxPublicationMs = 0, maxFrameMs = 0;
        const auto deadline = std::chrono::steady_clock::now() + 45s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto frameStart = std::chrono::steady_clock::now();
            timer.Tick();
            renderer.BeginFrame();
            const auto publicationStart = std::chrono::steady_clock::now();
            engine.Update(context);
            maxPublicationMs = std::max(maxPublicationMs,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - publicationStart).count());
            const auto& progress = engine.GetSceneLoadProgress();
            Check(progress.error.empty() && progress.created >= previousCreated && progress.created <= previousCreated + 32,
                "Scene publication error or unbounded entity batch");
            previousCreated = progress.created;
            if (progress.created)
                Check(!engine.GetRegistry().view<CameraComponent>().empty(), "Progressive scene has no camera");
            engine.Draw(context);
            const auto stats = engine.GetResources().GetTextureProgress();
            sawPartialTextures |= stats.total > 1 && stats.ready > 1 && stats.ready < stats.total;
            const int checker = renderer.mTextures.at(std::to_string(engine.GetResources().GetPlaceholderTexture()))->SrvHeapIndex;
            for (auto entity : engine.GetRegistry().view<MeshComponent>())
            {
                const MeshID id = engine.GetRegistry().get<MeshComponent>(entity).meshID;
                auto gpu = renderer.mGeometries.find(id);
                if (engine.GetResources().GetMesh(id).isPrimitive)
                    Check(gpu != renderer.mGeometries.end(), "Scene primitive waited in the GPU queue after its first draw");
                if (gpu == renderer.mGeometries.end()) continue;
                for (const auto& submesh : gpu->second->submeshes)
                    sawPlaceholder |= renderer.GetOrLoadMaterial(submesh.material)->DiffuseSrvHeapIndex == checker;
            }
            renderer.EndFrame();
            maxFrameMs = std::max(maxFrameMs,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count());
            ++frames;
            if (!engine.IsSceneLoading() && stats.total == 1001 && stats.ready == stats.total && Meshes(engine.GetRegistry()) == 1000 &&
                renderer.mGeometries.size() == 1001) break;
            std::this_thread::sleep_for(1ms);
        }
        const auto stats = engine.GetResources().GetTextureProgress();
        Check(!engine.IsSceneLoading() && Meshes(engine.GetRegistry()) == 1000 && stats.ready == 1001 && stats.failed == 0,
            "1000-texture scene did not finish streaming");
        Check(sawPlaceholder && sawPartialTextures && frames >= 250, "No visible placeholder/progressive texture loading or frames did not continue");
        // Parsing errors must preserve the current scene and arrive through progress.
        std::ofstream(startup / "bad.json") << "{ invalid JSON";
        Check(engine.LoadScene((startup / "bad.json").string(), error), "Error fixture was not queued");
        const auto errorDeadline = std::chrono::steady_clock::now() + 5s;
        while (engine.IsSceneLoading() && std::chrono::steady_clock::now() < errorDeadline)
        { frame(); std::this_thread::sleep_for(1ms); }
        Check(!engine.IsSceneLoading() && !engine.GetSceneLoadProgress().error.empty() && Meshes(engine.GetRegistry()) == 1000,
            "Parse failure cleared the previous scene or was not reported");
        renderer.FlushCommandQueue();
        if (diagnostics)
            for (UINT64 i = 0; i < diagnostics->GetNumStoredMessages(); ++i)
            {
                SIZE_T size = 0; diagnostics->GetMessage(i, nullptr, &size);
                std::vector<uint8_t> bytes(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
                diagnostics->GetMessage(i, message, &size);
                Check(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR && message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION,
                    message->pDescription);
            }
        // Shutdown also drains a scene parser with no publications yet.
        Check(engine.LoadScene((repo / "Chapter 9 Texturing/Try2/Scenes/TextureStreaming1000.json").string(), error), "Shutdown scene request rejected");
        engine.Shutdown();
        std::cout << "PASS: real Engine scene switch, 1000 DDS textures, placeholder draws, " << frames
            << " rendered frames; request_ms=" << requestMs << " max_publication_ms=" << maxPublicationMs
            << " max_frame_ms=" << maxFrameMs << "; parse-error preservation and shutdown\n";
    }
    DestroyWindow(window);
    UnregisterClass(wc.lpszClassName, instance);
}
