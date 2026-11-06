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
#include "Project.h"
#include "ImageIO.h"
#include "SceneSerializer.h"
#include "Editor/EditorPlayWindow.h"
#include "Editor/EditorProject.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <shellapi.h>
#include "../../tests/TerrainValidation.h"
#pragma comment(lib, "Shell32.lib")

#include <string>
#include <filesystem>
#include <stdexcept>
#include <iterator>

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
        const std::wstring& title = L"LS Engine", bool gameSmoke = false, bool terrainSmoke = false);
    /*TestApp(const TestApp& rhs) = delete;
    TestApp& operator=(const TestApp& rhs) = delete;
    ~TestApp();*/

    //virtual bool Initialize()override;
    // Editor "Play" process: the window title shows the scene once Play has started.
    void SetPlayWindow(const std::wstring& title) { mPlayTitle = title; mFreeCamera = true; }
    // End-to-end project check: "create" (new scene + PNG texture + save + reload + Play
    // window + Stop) or "reopen" (startup scene from the project file + Play + close window).
    void SetProjectSmoke(const std::string& mode) { mProjectSmoke = mode; }

private:
    std::wstring mPlayTitle;
    bool mPlayTitleShown = false;
    void UpdatePlayWindowTitle();
    std::string mProjectSmoke;
    int mProjectStage = 0, mProjectFrames = 0, mProjectTotalFrames = 0;
    size_t mProjectEntities = 0;
    void CheckProjectSmoke();
    std::unique_ptr<TerrainValidation> mTerrainValidation;
    void CheckScriptSmoke();
    void CheckGameSmoke();
    bool mGameSmoke = false;
    int mGameSteps = 0;
    float mBallMaxX = 0;
    bool mGoalSeen = false, mKickoffSeen = false;
    size_t mConfettiMax = 0;
    float mConfettiSpread = 0;
    bool mConfettiCleared = false;
    InputState GameSmokeInput() const;
    bool mScriptSmoke = false;
    int mScriptSmokeStage = 0, mScriptSmokeFrames = 0, mScriptSmokeTotalFrames = 0;
    //virtual void Init()override;
    virtual void Update(const FrameContext& context)override;
    virtual void PhysicsUpdate(const FrameContext& context)override;
    virtual void Draw(const FrameContext& context)override;
};

TestApp::TestApp(HINSTANCE hInstance, bool scriptSmoke, const std::string& gameScene,
    const std::wstring& title, bool gameSmoke, bool terrainSmoke)
    : Application(hInstance), mScriptSmoke(scriptSmoke), mGameSmoke(gameSmoke)
{
    mGameScene = gameScene;
    mStandalone = !gameScene.empty() && !terrainSmoke;
    if(terrainSmoke)mTerrainValidation=std::make_unique<TerrainValidation>();
    mMainWndCaption = title;
    ImGuiBridge::SetRuntimeMode(mStandalone);
}


// Blue (WASD) dribbles straight into the east goal while red (arrows) steps out of the way.
InputState TestApp::GameSmokeInput() const
{
    InputState input;
    input.keys[mGameSteps < 240 ? 'D' : mGameSteps < 420 ? 'W' : mGameSteps < 660 ? 'A' : 'S'] = true;
    if (mGameSteps < 240) input.keys[VK_UP] = true;
    return input;
}

void TestApp::PhysicsUpdate(const FrameContext& context)
{
	ZoneScopedN("PhysicsUpdate");
    if (mGameSmoke && mEngine->IsPlaying())
    {
        auto scripted = context;
        scripted.input = GameSmokeInput();
        ++mGameSteps;
        mEngine->PhysicsUpdate(scripted);
    }
    else mEngine->PhysicsUpdate(context);
}
void TestApp::Update(const FrameContext& context)
{
	ZoneScopedN("Update");
    if (mScriptSmoke || mGameSmoke || mTerrainValidation) mTimer.Start(); // Hidden validation still needs a ticking clock.
    if (mGameSmoke)
    {
        auto scripted = context;
        scripted.input = GameSmokeInput();
        mEngine->Update(scripted);
    }
    else mEngine->Update(context);
    if (mScriptSmoke) CheckScriptSmoke();
    if (mGameSmoke) CheckGameSmoke();
    if (!mPlayTitle.empty()) UpdatePlayWindowTitle();
    if (!mProjectSmoke.empty()) CheckProjectSmoke();
    if(mTerrainValidation)
    {
        try
        {
            if(mTerrainValidation->Tick(*mEngine,context,*static_cast<D3DRenderAdapter*>(mRenderAdapter.get())))
            {mTerrainValidation.reset();PostQuitMessage(0);}
        }
        catch(const std::exception& e){std::cerr<<"TERRAIN_SMOKE_FAIL: "<<e.what()<<std::endl;mTerrainValidation.reset();PostQuitMessage(2);}
    }
    if (mStandalone && context.input.keysPressed[VK_ESCAPE]) PostQuitMessage(0);
}
void TestApp::Draw(const FrameContext& context)
{
	ZoneScopedN("Draw");
	mEngine->Draw(context);
    if(mTerrainValidation)mTerrainValidation->AfterDraw(*static_cast<D3DRenderAdapter*>(mRenderAdapter.get()));
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

void TestApp::UpdatePlayWindowTitle()
{
    if (mPlayTitleShown) return;
    const auto& progress = mEngine->GetSceneLoadProgress();
    if (!progress.error.empty())
    {
        SetWindowTextW(mhMainWnd, (mPlayTitle + L" (error: " + AssetPaths::FromUtf8(progress.error).wstring() + L")").c_str());
        mPlayTitleShown = true;
    }
    else if (mEngine->IsPlaying())
    {
        SetWindowTextW(mhMainWnd, (mPlayTitle + L" | Esc closes").c_str());
        mPlayTitleShown = true;
    }
}

void TestApp::CheckProjectSmoke()
{
    namespace fs = std::filesystem;
    try
    {
        if (++mProjectTotalFrames > 20000) throw std::runtime_error("Project smoke timed out at stage " + std::to_string(mProjectStage));
        const auto check = [](bool valid, const std::string& error) { if (!valid) throw std::runtime_error(error); };
        auto* editor = Editor_GetContext();
        check(editor && editor->engine == mEngine.get(), "Editor is not connected to the engine");
        EditorPlayWindow::Update(*editor); // The editor UI does this every frame.
        if (mEngine->IsSceneLoading()) return;
        check(mEngine->GetSceneLoadProgress().error.empty(), "Scene load failed: " + mEngine->GetSceneLoadProgress().error);
        auto& reg = mEngine->GetRegistry();
        const auto& root = mEngine->GetAssetRoot();
        const auto find = [&](const char* name)
        {
            for (auto entity : reg.view<TagComponent>())
                if (reg.get<TagComponent>(entity).tag == name) return entity;
            throw std::runtime_error(std::string("Missing entity: ") + name);
        };
        const std::string textured = "Scenes/Textured.json", texture = "Assets/Textures/external-checker.png";
        const auto texturesReady = [&]
        {
            const auto progress = mEngine->GetResources().GetTextureProgress();
            check(progress.failed == 0, "A texture failed to load");
            return progress.total > 0 && progress.ready == progress.total;
        };
        const auto checkTexturedGround = [&]
        {
            const auto& source = reg.get<MeshSourceComponent>(find("Ground"));
            check(source.mesh.material && AssetPaths::ToUtf8(source.mesh.material->albedoTexture) == texture,
                "Ground lost its project-relative PNG texture");
        };
        const auto gameWindowPlaying = [&]
        {
            const auto title = EditorPlayWindow::WindowTitle();
            check(title.find(L"(error") == std::wstring::npos, "Play window reported an error: " + AssetPaths::ToUtf8(title));
            return title.find(L"Play: Textured") != std::wstring::npos && title.find(L"Esc closes") != std::wstring::npos;
        };

        if (mProjectSmoke == "create")
        {
            switch (mProjectStage)
            {
            case 0: // New project: starter scene from the project file.
                check(mEngine->GetScenePath() == Project::DefaultScene, "Startup scene is not " + std::string(Project::DefaultScene));
                find("Camera"); find("Sun"); find("Ground");
                for (const char* folder : Project::Folders) check(fs::is_directory(root / folder), std::string("Missing folder ") + folder);
                Project::WriteStarterScene(root / AssetPaths::FromUtf8(textured)); // File > New Scene
                EditorSceneIO::LoadScene(*editor, textured.c_str());
                mProjectStage = 1;
                return;
            case 1:
            {
                check(mEngine->GetScenePath() == textured, "New scene was not opened");
                // A PNG outside the project is imported into Assets/Textures (Inspector > Browse).
                std::vector<uint8_t> pixels(64 * 64 * 4);
                for (uint32_t y = 0; y < 64; ++y) for (uint32_t x = 0; x < 64; ++x)
                {
                    const bool dark = ((x / 8) + (y / 8)) % 2;
                    uint8_t* p = &pixels[(size_t(y) * 64 + x) * 4];
                    p[0] = dark ? 40 : 230; p[1] = dark ? 90 : 200; p[2] = dark ? 160 : 60; p[3] = 255;
                }
                const auto external = root.parent_path() / L"external-checker.png";
                ImageIO::Save(external, ImageIO::FromRgba8(64, 64, std::move(pixels)));
                const auto imported = EditorProject::ImportTexture(*editor, external);
                check(imported == texture, "Unexpected imported texture path: " + imported);
                fs::remove(external);
                const auto ground = find("Ground");
                auto material = *reg.get<MeshSourceComponent>(ground).mesh.material;
                material.albedoTexture = AssetPaths::FromUtf8(imported).wstring();
                check(EditorProject::ApplyMaterial(*editor, ground, material), "Material was not applied: " + editor->statusMessage);
                EditorSceneIO::SaveCurrentScene(*editor);
                check(editor->statusMessage.rfind("Saved", 0) == 0, "Save failed: " + editor->statusMessage);
                EditorProject::SetCurrentSceneAsStartup(*editor);
                check(Project::Open(mEngine->GetProject().file).startupScene == textured, "Startup scene was not stored");
                std::ifstream saved(root / AssetPaths::FromUtf8(textured));
                const std::string json((std::istreambuf_iterator<char>(saved)), {});
                check(json.find("\"albedo\": \"" + texture + "\"") != std::string::npos, "Scene does not store the relative texture path");
                check(json.find(AssetPaths::ToUtf8(root)) == std::string::npos, "Scene contains an absolute project path");
                EditorSceneIO::LoadScene(*editor, textured.c_str()); // Read back from disk.
                mProjectStage = 2;
                mProjectFrames = 0;
                return;
            }
            case 2:
                checkTexturedGround();
                if (!texturesReady()) return;
                mProjectEntities = reg.view<TagComponent>().size();
                check(EditorPlayWindow::Start(*editor), "Play window: " + editor->statusMessage);
                mProjectStage = 3;
                mProjectFrames = 0;
                return;
            case 3:
                check(EditorPlayWindow::IsRunning(), "Play window exited early: " + editor->statusMessage);
                check(editor->mode == EditorMode::Edit && !mEngine->IsPlaying(), "Editor left Edit mode");
                if (!gameWindowPlaying()) return;
                if (++mProjectFrames < 60) return; // The game runs while the editor keeps rendering.
                EditorPlayWindow::Stop(*editor);
                mProjectStage = 4;
                return;
            case 4:
                if (EditorPlayWindow::IsRunning()) return;
                check(editor->statusMessage.find("Play stopped") != std::string::npos, "Unexpected stop status: " + editor->statusMessage);
                check(editor->mode == EditorMode::Edit && !mEngine->IsPlaying(), "Editor is not in Edit mode after Stop");
                checkTexturedGround();
                check(reg.view<TagComponent>().size() == mProjectEntities, "Editor scene changed during Play");
                std::cout << "PROJECT_SMOKE_PASS: create; project " << AssetPaths::ToUtf8(mEngine->GetProject().file)
                    << ", new scene with imported PNG, relative save, reload, Play window, Stop" << std::endl;
                mProjectSmoke.clear();
                PostQuitMessage(0);
                return;
            }
        }
        else if (mProjectSmoke == "reopen")
        {
            switch (mProjectStage)
            {
            case 0:
                check(mEngine->GetScenePath() == textured, "Reopened project did not start with " + textured);
                checkTexturedGround();
                if (!texturesReady()) return;
                check(EditorPlayWindow::Start(*editor), "Play window: " + editor->statusMessage);
                mProjectStage = 1;
                return;
            case 1:
                check(EditorPlayWindow::IsRunning(), "Play window exited early: " + editor->statusMessage);
                if (!gameWindowPlaying()) return;
                {
                    // The user closes the game window instead of pressing Stop.
                    std::wstring title = EditorPlayWindow::WindowTitle();
                    HWND game = FindWindowW(L"MainWnd", title.c_str());
                    check(game != nullptr, "Game window not found");
                    PostMessageW(game, WM_CLOSE, 0, 0);
                }
                mProjectStage = 2;
                return;
            case 2:
                if (EditorPlayWindow::IsRunning()) return;
                check(editor->mode == EditorMode::Edit && !mEngine->IsPlaying(), "Editor is not in Edit mode after the game closed");
                checkTexturedGround();
                std::cout << "PROJECT_SMOKE_PASS: reopen; startup scene and PNG from the project file, Play window closed by the user" << std::endl;
                mProjectSmoke.clear();
                PostQuitMessage(0);
                return;
            }
        }
        else throw std::runtime_error("Unknown project smoke mode " + mProjectSmoke);
    }
    catch (const std::exception& error)
    {
        std::cerr << "PROJECT_SMOKE_FAIL: " << error.what() << std::endl;
        mProjectSmoke.clear();
        EditorPlayWindow::Shutdown();
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
        size_t confetti = 0;
        glm::vec3 firstConfetti{};
        for (auto entity : reg.view<TagComponent, TransformComponent>())
        {
            const auto& tag = reg.get<TagComponent>(entity).tag;
            const auto& p = reg.get<TransformComponent>(entity).position;
            if (tag.starts_with("Confetti"))
            {
                if (confetti++ == 0) firstConfetti = p;
                else mConfettiSpread = (std::max)(mConfettiSpread, glm::length(p - firstConfetti));
            }
            if (tag == "Ball")
            {
                mBallMaxX = (std::max)(mBallMaxX, p.x);
                if (p.x > 12.65f && std::abs(p.z) < 2.5f) mGoalSeen = true; // fully over the east goal line
                if (mGoalSeen && std::abs(p.x) < 0.01f && std::abs(p.z) < 0.01f) mKickoffSeen = true;
            }
            // Bodies stay on the field or inside a goal box (x in 12..14, z in -2.5..2.5).
            const bool inGoal = std::abs(p.x) <= 14.1f && std::abs(p.z) < 2.6f;
            if ((tag == "Ball" || tag == "Player" || tag == "Player2") &&
                ((std::abs(p.x) > 12.1f && !inGoal) || std::abs(p.z) > 8.1f || p.y < -0.1f))
                throw std::runtime_error("Body escaped the static field");
            if (tag == "Camera" && p != glm::vec3(0,19,-19)) throw std::runtime_error("Editor camera consumed gameplay input");
            if ((tag == "Field" || tag.ends_with("Border") || tag.ends_with("Wall")) && reg.get<RigidbodyComponent>(entity).type != RigidbodyType::Static)
                throw std::runtime_error("Field/border is not static");
            if (reg.all_of<ScriptComponent>(entity) && !mEngine->GetScripts().GetError(entity).empty())
                throw std::runtime_error(mEngine->GetScripts().GetError(entity));
        }
        // Goal confetti is spawned from Prefabs/ and removes itself after its lifetime.
        mConfettiMax = (std::max)(mConfettiMax, confetti);
        if (mConfettiMax > 0 && confetti == 0) mConfettiCleared = true;
        if (mGameSteps >= 840)
        {
            if (mBallMaxX < 3) throw std::runtime_error("Player did not physically push the ball");
            if (!mGoalSeen) throw std::runtime_error("Ball never entered the east goal");
            if (!mKickoffSeen) throw std::runtime_error("No kickoff after the goal");
            if (mConfettiMax == 0 || !mConfettiCleared) throw std::runtime_error("Goal confetti was not spawned and removed");
            if (mConfettiSpread < .5f) throw std::runtime_error("Confetti did not spread: particles share the same trajectory");
            std::cout << "GAME_SMOKE_PASS: standalone, Lua controls, two players, ball pushed into goal, kickoff, prefab spawn/destroy, static field/borders/goals; max_ball_x=" << mBallMaxX << " confetti=" << mConfettiMax << " confetti_spread=" << mConfettiSpread << std::endl;
            mGameSmoke = false; PostQuitMessage(0);
        }
    }
    catch (const std::exception& error) { std::cerr << "GAME_SMOKE_FAIL: " << error.what() << std::endl; mGameSmoke = false; PostQuitMessage(2); }
}

namespace
{
    // Command line:
    //   Try2.exe                                   editor with the sample project (engine demos)
    //   Try2.exe --project <folder|file.lsproject> editor with a project
    //   Try2.exe --new-project <folder> [--name N] create a project, then open it in the editor
    //   --show-editor                              start with the editor UI visible (otherwise ~ toggles it)
    //   Try2.exe --play --project <p> [--scene S]  game window (what the editor's Play starts)
    //            [--no-hot-reload]
    //   Try2.exe [--project <p>] --game <manifest> standalone game from a game.json manifest
    //   Test modes: --lua-smoke, --terrain-smoke, --game-smoke <manifest>, --project-smoke create|reopen
    struct Options
    {
        std::wstring project, newProject, projectName, scene, game;
        bool play = false, noHotReload = false, gameSmoke = false, luaSmoke = false, terrainSmoke = false, showEditor = false;
        std::string projectSmoke;
    };

    Options ParseOptions(const std::vector<std::wstring>& args)
    {
        Options o;
        const auto value = [&](size_t& i) -> const std::wstring&
        {
            if (i + 1 >= args.size()) throw std::runtime_error("Missing value after " + AssetPaths::ToUtf8(args[i]));
            return args[++i];
        };
        for (size_t i = 1; i < args.size(); ++i)
        {
            const auto& a = args[i];
            if (a == L"--project") o.project = value(i);
            else if (a == L"--new-project") o.newProject = value(i);
            else if (a == L"--name") o.projectName = value(i);
            else if (a == L"--play") o.play = true;
            else if (a == L"--scene") o.scene = value(i);
            else if (a == L"--no-hot-reload") o.noHotReload = true;
            else if (a == L"--show-editor") o.showEditor = true;
            else if (a == L"--game" || a == L"--game-smoke") { o.gameSmoke = a == L"--game-smoke"; o.game = value(i); }
            else if (a == L"--lua-smoke") o.luaSmoke = true;
            else if (a == L"--terrain-smoke") o.terrainSmoke = true;
            else if (a == L"--project-smoke") o.projectSmoke = AssetPaths::ToUtf8(value(i));
            else throw std::runtime_error("Unknown option " + AssetPaths::ToUtf8(a) +
                "\nUsage: Try2.exe [--project <folder|.lsproject>] [--new-project <folder> [--name N]] "
                "[--play [--scene <scene.json>]] [--game <game.json>]");
        }
        if (!o.project.empty() && !o.newProject.empty()) throw std::runtime_error("Use either --project or --new-project");
        if (o.play && !o.game.empty()) throw std::runtime_error("Use either --play or --game");
        return o;
    }
}

int main(int argc, char** argv)
{
    bool quietErrors = false; // Hidden test runs report on stderr instead of a dialog.
    try
    {
        ConfigureRuntimeDirectory(); // Engine files (shaders) are read relative to the engine folder.
        int argumentCount = 0;
        auto** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (!arguments) throw std::runtime_error("Cannot parse command line");
        std::vector<std::wstring> args(arguments, arguments + argumentCount);
        LocalFree(arguments);
        const Options options = ParseOptions(args);
        quietErrors = options.terrainSmoke || !options.projectSmoke.empty();

        // Every run works inside one project; without --project it is the engine's sample project.
        ProjectInfo project;
        if (!options.newProject.empty())
        {
            const std::filesystem::path folder(options.newProject);
            const auto name = options.projectName.empty() ? folder.filename().wstring() : options.projectName;
            project = Project::Create(folder, AssetPaths::ToUtf8(name));
            std::cout << "Created project " << AssetPaths::ToUtf8(project.file) << std::endl;
        }
        else if (!options.project.empty()) project = Project::Open(options.project);
        else project = Project::OpenSample();
        Project::SetActive(project);

        std::string gameScene;
        std::wstring title = L"LS Engine | " + AssetPaths::FromUtf8(project.name).wstring();
        if (!options.game.empty())
        {
            const auto manifest = AssetPaths::Resolve(AssetPaths::FindRoot(), AssetPaths::ToUtf8(std::filesystem::path(options.game)));
            std::ifstream file(manifest);
            if (!file) throw std::runtime_error("Cannot open game manifest");
            const auto data = nlohmann::json::parse(file);
            gameScene = data.at("scene").get<std::string>();
            if (gameScene.empty()) throw std::runtime_error("Game scene must not be empty");
            const auto controls = data.value("controls", "");
            title = AssetPaths::FromUtf8(data.value("title", "LS Engine Game") + (controls.empty() ? "" : " | " + controls)).native();
        }
        std::wstring playTitle;
        if (options.play)
        {
            gameScene = options.scene.empty() ? project.startupScene : AssetPaths::ToUtf8(std::filesystem::path(options.scene));
            if (gameScene.empty()) throw std::runtime_error("The project has no startupScene; pass --scene <scene.json>");
            playTitle = AssetPaths::FromUtf8(project.name).wstring() + L" | Play: " +
                AssetPaths::FromUtf8(gameScene).stem().wstring();
            title = playTitle + L" (loading)";
        }

        HINSTANCE hInstance = GetModuleHandle(nullptr);
        TestApp theApp(hInstance, options.luaSmoke, gameScene, title, options.gameSmoke, options.terrainSmoke);
        if (options.play) theApp.SetPlayWindow(playTitle);
        if (!options.projectSmoke.empty()) theApp.SetProjectSmoke(options.projectSmoke);
        if (!theApp.Initialize())
            return 0;
        if (options.noHotReload) theApp.GetEngine()->GetScripts().SetHotReload(false);

        ImGuiBridge::SetEngine(theApp.GetEngine());
        if (options.luaSmoke || options.terrainSmoke || !options.projectSmoke.empty() || (options.showEditor && !options.play && options.game.empty()))
        {
            // Exercise the real editor initialization/toggle path too.
            InputState toggle{};
            toggle.keysPressed[VK_OEM_3] = true;
            ImGuiBridge::ProcessInput(toggle);
        }

        const int code = theApp.Run();
        EditorPlayWindow::Shutdown(); // A running game window closes with the editor.
        return code;
    }
    catch (DxException& e)
    {
        if (quietErrors) { std::cerr << "Startup D3D failure: HRESULT " << e.ErrorCode << std::endl; return EXIT_FAILURE; }
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return EXIT_FAILURE;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        if (quietErrors) return EXIT_FAILURE;
        MessageBoxA(nullptr, e.what(), "Startup failed", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
}
