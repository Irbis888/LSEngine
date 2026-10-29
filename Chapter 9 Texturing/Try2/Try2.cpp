// Try2.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include <iostream>
#include <entt/entt.hpp>
#include "Application.h"
#include "ImGuiBridge.h"
#include "ScriptSystem.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorSceneIO.h"
#include "AssetPaths.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")

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
    TestApp(HINSTANCE hInstance, bool scriptSmoke = false, const std::string& gameScene = {},
        const std::wstring& title = L"LS Engine", bool gameSmoke = false);
    /*TestApp(const TestApp& rhs) = delete;
    TestApp& operator=(const TestApp& rhs) = delete;
    ~TestApp();*/

    //virtual bool Initialize()override;

private:
    void CheckScriptSmoke();
    void CheckGameSmoke();
    bool mGameSmoke = false;
    int mGameSteps = 0;
    float mBallMaxX = 0;
    bool mScriptSmoke = false;
    int mScriptSmokeStage = 0, mScriptSmokeFrames = 0, mScriptSmokeTotalFrames = 0;
    //virtual void Init()override;
    virtual void Update(const FrameContext& context)override;
    virtual void PhysicsUpdate(const FrameContext& context)override;
    virtual void Draw(const FrameContext& context)override;
};

TestApp::TestApp(HINSTANCE hInstance, bool scriptSmoke, const std::string& gameScene,
    const std::wstring& title, bool gameSmoke)
    : Application(hInstance), mScriptSmoke(scriptSmoke), mGameSmoke(gameSmoke)
{
    mGameScene = gameScene;
    mStandalone = !gameScene.empty();
    mMainWndCaption = title;
    ImGuiBridge::SetRuntimeMode(mStandalone);
}


void TestApp::PhysicsUpdate(const FrameContext& context)
{
	ZoneScopedN("PhysicsUpdate");
    if (mGameSmoke && mEngine->IsPlaying())
    {
        auto scripted = context;
        scripted.input = {};
        scripted.input.keys[mGameSteps < 240 ? 'D' : mGameSteps < 420 ? 'W' : mGameSteps < 660 ? 'A' : 'S'] = true;
        ++mGameSteps;
        mEngine->PhysicsUpdate(scripted);
    }
    else mEngine->PhysicsUpdate(context);
}
void TestApp::Update(const FrameContext& context)
{
	ZoneScopedN("Update");
    if (mScriptSmoke || mGameSmoke) mTimer.Start(); // Hidden validation still needs a ticking clock.
    if (mGameSmoke)
    {
        auto scripted = context;
        scripted.input = {};
        scripted.input.keys[mGameSteps < 240 ? 'D' : mGameSteps < 420 ? 'W' : mGameSteps < 660 ? 'A' : 'S'] = true;
        mEngine->Update(scripted);
    }
    else mEngine->Update(context);
    if (mScriptSmoke) CheckScriptSmoke();
    if (mGameSmoke) CheckGameSmoke();
    if (mStandalone && context.input.keysPressed[VK_ESCAPE]) PostQuitMessage(0);
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

void TestApp::CheckGameSmoke()
{
    try
    {
        if (!mEngine->GetSceneLoadProgress().error.empty()) throw std::runtime_error(mEngine->GetSceneLoadProgress().error);
        if (!mEngine->IsPlaying()) return;
        if (ImGuiBridge::IsVisible() || ImGuiBridge::IsLicensed()) throw std::runtime_error("Editor enabled in game mode");
        auto& reg = mEngine->GetRegistry();
        for (auto entity : reg.view<TagComponent, TransformComponent>())
        {
            const auto& tag = reg.get<TagComponent>(entity).tag;
            const auto& p = reg.get<TransformComponent>(entity).position;
            if (tag == "Ball") mBallMaxX = (std::max)(mBallMaxX, p.x);
            if ((tag == "Ball" || tag == "Player") && (std::abs(p.x) > 12.1f || std::abs(p.z) > 8.1f || p.y < -0.1f))
                throw std::runtime_error("Body escaped the static field");
            if (tag == "Camera" && p != glm::vec3(0,19,-19)) throw std::runtime_error("Editor camera consumed gameplay input");
            if ((tag == "Field" || tag.ends_with("Border")) && reg.get<RigidbodyComponent>(entity).type != RigidbodyType::Static)
                throw std::runtime_error("Field/border is not static");
            if (reg.all_of<ScriptComponent>(entity) && !mEngine->GetScripts().GetError(entity).empty())
                throw std::runtime_error(mEngine->GetScripts().GetError(entity));
        }
        if (mGameSteps >= 840)
        {
            if (mBallMaxX < 3) throw std::runtime_error("Player did not physically push the ball");
            std::cout << "GAME_SMOKE_PASS: standalone, Lua controls, player hitbox, ball pushed, static field/borders; max_ball_x=" << mBallMaxX << std::endl;
            mGameSmoke = false; PostQuitMessage(0);
        }
    }
    catch (const std::exception& error) { std::cerr << "GAME_SMOKE_FAIL: " << error.what() << std::endl; mGameSmoke = false; PostQuitMessage(2); }
}

int main(int argc, char** argv)
{   

    try
    {
        ConfigureRuntimeDirectory();
        std::string gameScene;
        std::wstring title = L"LS Engine";
        bool gameSmoke = false;
        int argumentCount = 0;
        auto** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (!arguments) throw std::runtime_error("Cannot parse command line");
        std::vector<std::wstring> options(arguments, arguments + argumentCount);
        LocalFree(arguments);
        if (options.size() > 1 && (options[1] == L"--game" || options[1] == L"--game-smoke"))
        {
            if (options.size() != 3) throw std::runtime_error("Usage: Try2.exe --game Games/BallField/game.json");
            gameSmoke = options[1] == L"--game-smoke";
            const auto manifest = AssetPaths::Resolve(AssetPaths::FindRoot(), AssetPaths::ToUtf8(std::filesystem::path(options[2])));
            std::ifstream file(manifest);
            if (!file) throw std::runtime_error("Cannot open game manifest");
            const auto data = nlohmann::json::parse(file);
            gameScene = data.at("scene").get<std::string>();
            if (gameScene.empty()) throw std::runtime_error("Game scene must not be empty");
            const auto controls = data.value("controls", "");
            title = AssetPaths::FromUtf8(data.value("title", "LS Engine Game") + (controls.empty() ? "" : " | " + controls)).native();
        }
        HINSTANCE hInstance = GetModuleHandle(nullptr);
        TestApp theApp(hInstance, argc == 2 && std::string_view(argv[1]) == "--lua-smoke", gameScene, title, gameSmoke);
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


