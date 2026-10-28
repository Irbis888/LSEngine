// Try2.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include <iostream>
#include <entt/entt.hpp>
#include "Application.h"
#include "ImGuiBridge.h"
#include "ScriptSystem.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorSceneIO.h"

#include <string>
#include <filesystem>
#include <stdexcept>

namespace
{
    void ConfigureRuntimeDirectory()
    {
        namespace fs = std::filesystem;
        const auto hasAssets = [](const fs::path& directory)
        {
            return fs::is_regular_file(directory / "Shaders/Default.hlsl") &&
                fs::is_directory(directory / "Scenes");
        };
        if (hasAssets(fs::current_path())) return;

        std::wstring executable(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size())
            throw std::runtime_error("Could not determine the executable directory");
        executable.resize(length);
        for (fs::path directory = fs::path(executable).parent_path(); !directory.empty();)
        {
            if (hasAssets(directory))
            {
                fs::current_path(directory);
                return;
            }
            const auto parent = directory.parent_path();
            if (parent == directory) break;
            directory = parent;
        }
        throw std::runtime_error("Could not find runtime assets: Shaders/Default.hlsl and Scenes near the executable");
    }
}

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")



class TestApp : public Application
{
public:
    TestApp(HINSTANCE hInstance, bool scriptSmoke = false);
    /*TestApp(const TestApp& rhs) = delete;
    TestApp& operator=(const TestApp& rhs) = delete;
    ~TestApp();*/

    //virtual bool Initialize()override;

private:
    void CheckScriptSmoke();
    bool mScriptSmoke = false;
    int mScriptSmokeStage = 0, mScriptSmokeFrames = 0, mScriptSmokeTotalFrames = 0;
    //virtual void Init()override;
    virtual void Update(const FrameContext& context)override;
    virtual void PhysicsUpdate(const FrameContext& context)override;
    virtual void Draw(const FrameContext& context)override;
};

TestApp::TestApp(HINSTANCE hInstance, bool scriptSmoke)
    : Application(hInstance), mScriptSmoke(scriptSmoke)
{
}


void TestApp::PhysicsUpdate(const FrameContext& context)
{
	ZoneScopedN("PhysicsUpdate");
    mEngine->PhysicsUpdate(context);
}
void TestApp::Update(const FrameContext& context)
{
	ZoneScopedN("Update");
    if (mScriptSmoke) mTimer.Start(); // Hidden smoke runs still need a ticking game clock.
	mEngine->Update(context);
    if (mScriptSmoke) CheckScriptSmoke();
}
void TestApp::Draw(const FrameContext& context)
{
	ZoneScopedN("Draw");
	mEngine->Draw(context);
}



// Opt-in integration check through the ordinary Application, renderer, editor
// Play/Stop handlers and frame loop. No replacement system or desktop input.
void TestApp::CheckScriptSmoke()
{
    try
    {
        if (++mScriptSmokeTotalFrames > 10000) throw std::runtime_error("Lua application smoke timed out");
        if (mEngine->IsSceneLoading()) return;
        if (!mEngine->GetSceneLoadProgress().error.empty()) throw std::runtime_error(mEngine->GetSceneLoadProgress().error);
        const auto find = [&](const char* name)
        {
            for (auto entity : mEngine->GetRegistry().view<TagComponent>())
                if (mEngine->GetRegistry().get<TagComponent>(entity).tag == name) return entity;
            throw std::runtime_error(std::string("Missing Lua demo entity: ") + name);
        };
        const auto check = [](bool valid, const char* error) { if (!valid) throw std::runtime_error(error); };
        auto* editor = Editor_GetContext();
        check(editor && editor->engine == mEngine.get(), "Editor is not connected to the actual application engine");
        if (mScriptSmokeStage == 0)
        {
            std::string error;
            check(mEngine->LoadScene(EditorSceneIO::LuaDemoPath(), error), "Could not open Lua demo");
            mScriptSmokeStage = 1;
            return;
        }
        const auto slow = find("SpinnerSlow"), fast = find("SpinnerFast");
        const auto& a = mEngine->GetRegistry().get<TransformComponent>(slow);
        const auto& b = mEngine->GetRegistry().get<TransformComponent>(fast);
        if (mScriptSmokeStage == 1 || mScriptSmokeStage == 3)
        {
            check(a.rotation == glm::vec3(0) && b.rotation == glm::vec3(0), "Edit/restored scene was changed by callbacks");
            EditorSceneIO::BeginPlay(*editor);
            check(mEngine->IsPlaying() && editor->mode == EditorMode::Play, "Actual editor Play failed");
            ++mScriptSmokeStage;
            mScriptSmokeFrames = 0;
        }
        else if (mScriptSmokeStage == 2 || mScriptSmokeStage == 4)
        {
            check(mEngine->GetScripts().GetError(slow).empty() && mEngine->GetScripts().GetError(fast).empty(), "Demo script failed");
            if (++mScriptSmokeFrames < 120) return;
            check(a.rotation.y > 0 && a.rotation.x == 0 && b.rotation.x < 0 && b.rotation.y == 0,
                "Application did not run independent spinner instances");
            EditorSceneIO::EndPlay(*editor);
            check(!mEngine->IsPlaying() && editor->mode == EditorMode::Edit, "Actual editor Stop failed");
            mScriptSmokeStage = mScriptSmokeStage == 2 ? 3 : 5;
        }
        else if (mScriptSmokeStage == 5)
        {
            check(a.rotation == glm::vec3(0) && b.rotation == glm::vec3(0), "Second Stop did not restore the snapshot");
            std::cout << "APP_LUA_SMOKE_PASS: actual Application + D3D12 + editor + Lua demo + Play/Stop/Play/Stop" << std::endl;
            mScriptSmoke = false;
            PostQuitMessage(0);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "APP_LUA_SMOKE_FAIL: " << error.what() << std::endl;
        mScriptSmoke = false;
        PostQuitMessage(2);
    }
}

int main(int argc, char** argv)
{   

    try
    {
        ConfigureRuntimeDirectory();
        HINSTANCE hInstance = GetModuleHandle(nullptr);
        TestApp theApp(hInstance, argc == 2 && std::string_view(argv[1]) == "--lua-smoke");
        if (!theApp.Initialize())
            return 0;

        ImGuiBridge::SetEngine(theApp.GetEngine());
        if (argc == 2 && std::string_view(argv[1]) == "--lua-smoke")
        {
            // Exercise the real editor initialization/toggle path too.
            InputState toggle{};
            toggle.keysPressed[VK_OEM_3] = true;
            ImGuiBridge::ProcessInput(toggle);
        }

        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return EXIT_FAILURE;
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Startup failed", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
}


