#include "Engine.h"
#include "D3DRenderAdapter.h"
#include "ScriptSystem.h"
#include "SceneSerializer.h"
#include "AssetPaths.h"
#include "Editor/EditorSceneIO.h"
#include "Editor/EditorContext.h"
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
static bool captureInput = false;
namespace ImGuiBridge { bool WantsCaptureInput() { return captureInput; } }
bool Editor_IsPhysicsEnabled() { return false; }
static void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static void Write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary); file << text;
    Check(file.good(), "Could not create Lua fixture");
}
static entt::entity Find(entt::registry& registry, const std::string& tag)
{
    for (auto entity : registry.view<TagComponent>()) if (registry.get<TagComponent>(entity).tag == tag) return entity;
    return entt::null;
}
static entt::entity Add(Engine& engine, const std::string& tag, const std::string& file)
{
    auto& registry = engine.GetRegistry();
    const auto entity = registry.create();
    registry.emplace<TagComponent>(entity, tag);
    registry.emplace<TransformComponent>(entity, glm::vec3(0), glm::vec3(0), glm::vec3(1));
    if (!file.empty()) registry.emplace<ScriptComponent>(entity, "Scripts/" + file, true, ScriptParameters{});
    return entity;
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 3, "Expected project and fixture root");
        const auto project = fs::absolute(AssetPaths::FromUtf8(argv[1]));
        const auto fixtures = fs::absolute(AssetPaths::FromUtf8(argv[2]));
        const auto assets = fixtures / L"Lua assets тест";
        fs::create_directories(assets / "Scenes"); fs::create_directories(assets / "Scripts");
        fs::copy(project / "Scripts", assets / "Scripts", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        fs::copy_file(project / "Scenes/LuaDemo.json", assets / "Scenes/LuaDemo.json", fs::copy_options::overwrite_existing);
        auto variedCase = AssetPaths::ToUtf8(assets / "Scripts/Rotate.lua");
        for (auto& c : variedCase) if (c >= 'A' && c <= 'Z') c = char(c + 'a' - 'A');
        Check(AssetPaths::ScriptPath(assets, variedCase).starts_with("Scripts/"), "Windows path case broke relative script paths");
        fs::current_path(project); // Shader paths belong to the real renderer.
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = DefWindowProcW; cls.lpszClassName = L"LuaEngineTests";
        Check(RegisterClassW(&cls) != 0, "Window registration failed");
        const auto window = CreateWindowW(cls.lpszClassName, L"Lua engine integration", WS_OVERLAPPEDWINDOW,
            0, 0, 96, 96, nullptr, nullptr, instance, nullptr);
        Check(window != nullptr, "Could not create test window");
        {
            D3DRenderAdapter renderer; renderer.Init(window, 96, 96);
            GameTimer timer; timer.Reset(); timer.Tick();
            Engine engine(&renderer, assets); engine.Init(timer);
            EditorContext editor; editor.engine = &engine; editor.registry = &engine.GetRegistry(); editor.resources = &engine.GetResources();
            InputState input{};
            double updateDt = 0;
            const auto frame = [&](int fixedSteps = 1)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2)); timer.Tick();
                FrameContext context{ timer, input, 1.0f / 60 };
                for (int i = 0; i < fixedSteps; ++i) engine.PhysicsUpdate(context);
                renderer.BeginFrame(); engine.Update(context); engine.Draw(context); renderer.EndFrame();
                updateDt += timer.DeltaTime();
                std::fill(std::begin(input.keysPressed), std::end(input.keysPressed), false);
                std::fill(std::begin(input.keysReleased), std::end(input.keysReleased), false);
            };
            const auto waitScene = [&]
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                while (engine.IsSceneLoading() && std::chrono::steady_clock::now() < deadline) frame();
                Check(!engine.IsSceneLoading() && engine.GetSceneLoadProgress().error.empty(), "Scene failed to load");
            };
            waitScene();
            std::string error;
            Check(engine.LoadScene("Scenes/LuaDemo.json", error), "Lua demo request rejected"); waitScene();
            auto& reg = engine.GetRegistry();
            const auto slow = Find(reg, "SpinnerSlow"), fast = Find(reg, "SpinnerFast");
            Check(slow != entt::null && fast != entt::null, "Lua demo did not publish entities");
            for (int i = 0; i < 3; ++i) frame();
            Check(reg.get<TransformComponent>(slow).rotation == glm::vec3(0), "Script ran in Edit");
            EditorSceneIO::BeginPlay(editor);
            Check(editor.mode == EditorMode::Play && engine.IsPlaying(), "Actual editor BeginPlay failed");
            updateDt = 0;
            for (int i = 0; i < 5; ++i) frame(3);
            Check(std::abs(reg.get<TransformComponent>(slow).rotation.y - updateDt * .6) < 1e-4,
                "OnUpdate dt or first spinner parameter incorrect");
            Check(std::abs(reg.get<TransformComponent>(fast).rotation.x + updateDt * 1.8) < 1e-4,
                "Shared file instances did not keep different parameters");
            input.keysPressed['O'] = true; frame();
            const auto door = Find(reg, "Door"); const auto oldDoorY = reg.get<TransformComponent>(door).position.y;
            for (int i = 0; i < 10; ++i) frame();
            Check(reg.get<TransformComponent>(door).position.y > oldDoorY, "Demo event did not open door");
            const auto keyboard = Find(reg, "KeyboardCube");
            const float oldX = reg.get<TransformComponent>(keyboard).position.x;
            input.keys[VK_RIGHT] = true; captureInput = true; frame();
            Check(reg.get<TransformComponent>(keyboard).position.x == oldX, "Editor-captured keyboard reached Lua");
            captureInput = false; frame(); input.keys[VK_RIGHT] = false;
            Check(reg.get<TransformComponent>(keyboard).position.x > oldX, "InputState key-down did not reach Lua");
            input.keysPressed['X'] = true; frame();
            Check(Find(reg, "DeleteMe") == entt::null, "Demo deferred deletion did not execute");
            const auto oldSession = engine.GetScripts().GetRuntime().Reference(slow);
            EditorSceneIO::EndPlay(editor);
            Check(!engine.IsPlaying() && editor.mode == EditorMode::Edit, "Stop left scripts active");
            Check(!engine.GetScripts().GetRuntime().Valid(oldSession), "Stop did not invalidate entity references");
            waitScene();
            Check(Find(reg, "DeleteMe") != entt::null, "Stop did not restore deleted entity");
            const auto restoredSlow = Find(reg, "SpinnerSlow");
            Check(reg.get<TransformComponent>(restoredSlow).rotation == glm::vec3(0), "Snapshot rotation was not restored");
            Check(std::get<std::string>(reg.get<ScriptComponent>(restoredSlow).parameters.at("label")) == "slow independent instance",
                "String parameter did not round-trip");
            Check(std::get<glm::vec3>(reg.get<ScriptComponent>(restoredSlow).parameters.at("axis")) == glm::vec3(0,1,0),
                "Vec3 parameter did not round-trip");
            EditorSceneIO::BeginPlay(editor); frame();
            Check(engine.IsPlaying(), "Second Play failed"); EditorSceneIO::EndPlay(editor); waitScene();
            std::cout << "PASS: real D3D12 frames, demo, editor Play/Stop/Play, dt, parameters, input capture, events, deletion and restore\n";

            Write(assets / "Scripts/State.lua", R"(privateGlobal = 0; local privateLocal = 0
return { parameters={speed=1}, OnInit=function(self) assert(self.entity.__index==nil); math.randomseed(42);
self.entity:SetScale(self.entity:GetScale()+Vec3(1,0,0)) end,
OnUpdate=function(self,dt) privateGlobal=privateGlobal+1; privateLocal=privateLocal+1;
 self.entity:SetPosition(Vec3(privateGlobal,privateLocal,dt*self.params.speed));
 self.entity:SetRotation(Vec3(math.random(),0,0)) end })");
            Write(assets / "Scripts/Receiver.lua", R"(return {OnEvent=function(self,name,payload,sender)
if name=='First' then assert(payload.value==1 and payload.pos.x==1 and payload.flag and payload.text=='ok');
 self.entity:SetPosition(Vec3(1,0,0)); self.entity:SendEvent('Second',{})
else self.entity:SetPosition(Vec3(2,0,0)) end end})");
            Write(assets / "Scripts/Sender.lua", R"(return {OnInit=function(self) self.sent=false end, OnUpdate=function(self,dt)
if not self.sent then local payload={value=1,pos=Vec3(1,2,3),flag=true,text='ok'};
 Entity.FindByName('Receiver'):SendEvent('First',payload); payload.value=99;payload.pos.x=99;self.sent=true end end})");
            Write(assets / "Scripts/Fixed.lua", R"(return {OnInit=function(self) self.presses=0 end,
OnUpdate=function(self,dt) if Input.IsKeyPressed(Key.Space) then self.presses=self.presses+1 end end,
OnFixedUpdate=function(self,dt) assert(math.abs(dt-1/60)<0.000001); assert(not Input.IsKeyPressed(Key.Space));
 local p=self.entity:GetPosition(); self.entity:SetPosition(Vec3(p.x+1,self.presses,dt)); end})");
            Write(assets / "Scripts/Cleanup.lua", R"(return {OnDestroy=function(self)
assert(self.entity:IsValid()); local p=self.entity:GetPosition();local marker=Entity.FindByName('CleanupMarker');
marker:SetPosition(marker:GetPosition()+Vec3(1,0,0)) end})");
            Write(assets / "Scripts/Loop.lua", "return {OnUpdate=function(self,dt) while true do end end}");
            Write(assets / "Scripts/Error.lua", "return {OnUpdate=function(self,dt) error('deliberate callback failure') end}");
            Write(assets / "Scripts/Syntax.lua", "return { OnUpdate = this is invalid Lua }");
            Write(assets / "Scripts/TopLoop.lua", "while true do end");
            Write(assets / "Scripts/Metadata.lua", "Entity.FindByName('Door'):Destroy(); return {}");
            Write(assets / "Scripts/Impulse.lua", R"(return {OnInit=function(self) self.applied=false end,
OnFixedUpdate=function(self,dt) if not self.applied then self.entity:ApplyImpulse(Vec3(4,0,0)); self.applied=true end end})");
            Write(assets / AssetPaths::FromUtf8("Scripts/Юникод.lua"), "return {OnUpdate=function(self,dt) self.entity:SetScale(Vec3(3,4,5)) end}");
            const auto a = Add(engine, "StateA", "State.lua"), b = Add(engine, "StateB", "State.lua");
            const auto receiver = Add(engine, "Receiver", "Receiver.lua"); Add(engine, "Sender", "Sender.lua");
            const auto fixed = Add(engine, "Fixed", "Fixed.lua");
            const auto marker = Add(engine, "CleanupMarker", ""); const auto cleanup = Add(engine, "Cleanup", "Cleanup.lua");
            const auto loop = Add(engine, "Loop", "Loop.lua"), failed = Add(engine, "Error", "Error.lua");
            const auto syntax = Add(engine, "Syntax", "Syntax.lua"), missing = Add(engine, "Missing", "Missing.lua");
            const auto unicode = Add(engine, "Unicode", "Юникод.lua");
            const auto impulseBody = Add(engine, "ImpulseDynamic", "Impulse.lua");
            auto& dynamicBody = reg.emplace<RigidbodyComponent>(impulseBody);
            dynamicBody.mass = 2; dynamicBody.useGravity = false;
            const auto staticBody = Add(engine, "ImpulseStatic", "Impulse.lua");
            reg.emplace<RigidbodyComponent>(staticBody).type = RigidbodyType::Static;
            Check(!engine.GetScripts().Describe("Scripts/TopLoop.lua").error.empty(), "Metadata infinite loop was not stopped");
            Check(!engine.GetScripts().Describe("Scripts/Metadata.lua").error.empty(), "Metadata could access mutable scene API");
            Check(Find(reg, "Door") != entt::null, "Metadata deleted a scene entity");
            Check(engine.BeginPlay(error), "Fixture Play failed");
            input.keysPressed[VK_SPACE] = true; frame(3);
            Check(reg.get<TransformComponent>(a).position.x == 1 && reg.get<TransformComponent>(b).position.x == 1,
                "Per-file globals/locals leaked between instances");
            Check(reg.get<TransformComponent>(a).rotation.x == reg.get<TransformComponent>(b).rotation.x,
                "math.random/randomseed state leaked between instances");
            Check(reg.get<TransformComponent>(receiver).position.x == 1, "Payload was not copied or event delivery recursed");
            Check(reg.get<TransformComponent>(fixed).position.x == 3 && reg.get<TransformComponent>(fixed).position.y == 0,
                "OnFixedUpdate did not run before physics with fixed dt");
            Check(reg.get<RigidbodyComponent>(impulseBody).velocity.x == 2 &&
                std::abs(reg.get<TransformComponent>(impulseBody).position.x - .1f) < 1e-5f,
                "Impulse ignored inverse mass or ran after integration");
            Check(reg.get<RigidbodyComponent>(staticBody).velocity == glm::vec3(0), "Impulse moved a static body");
            Check(reg.get<TransformComponent>(unicode).scale == glm::vec3(3,4,5), "Unicode Lua filename did not execute");
            reg.get<ScriptComponent>(a).enabled = false; frame(3);
            Check(reg.get<TransformComponent>(receiver).position.x == 2, "Event emitted by an event was not deferred to next pass");
            Check(reg.get<TransformComponent>(a).position.x == 1 && reg.get<TransformComponent>(b).position.x == 2,
                "Disabled instance received an update or shared globals");
            Check(reg.get<TransformComponent>(fixed).position.y == 1, "One key press was repeated/lost across fixed steps");
            reg.get<ScriptComponent>(a).enabled = true; frame();
            Check(reg.get<TransformComponent>(a).position.x == 2 && reg.get<TransformComponent>(a).scale.x == 2,
                "Re-enable reset state or called OnInit twice");
            Check(engine.GetScripts().GetError(loop).find("instruction limit") != std::string::npos, "Infinite callback was not stopped");
            Check(engine.GetScripts().GetError(failed).find("stack traceback") != std::string::npos, "Callback error lacks traceback");
            Check(engine.GetScripts().GetError(syntax).find("Syntax") != std::string::npos &&
                engine.GetScripts().GetError(missing).find("Missing.lua") != std::string::npos, "Missing/syntax errors lack entity/path");
            reg.remove<ScriptComponent>(cleanup);
            Check(reg.get<TransformComponent>(marker).position.x == 1, "Component removal did not call OnDestroy before removal");
            const auto stale = engine.GetScripts().GetRuntime().Reference(cleanup);
            engine.GetScripts().DestroyEntity(cleanup);
            const auto replacement = Add(engine, "Replacement", "");
            Check(!engine.GetScripts().GetRuntime().Valid(stale), "Entity version reuse accepted a stale reference");
            (void)replacement;
            Write(assets / "Scripts/Stale.lua", R"(return {OnInit=function(self) self.old=Entity.FindByName('Victim') end,
OnUpdate=function(self,dt) if self.old:IsValid() then self.old:Destroy() else self.old:GetPosition() end end})");
            Add(engine, "Victim", "Cleanup.lua"); const auto observer = Add(engine, "Observer", "Stale.lua");
            frame(); frame();
            Check(engine.GetScripts().GetError(observer).find("deleted") != std::string::npos, "Stale Lua userdata did not report a protected error");
            Check(reg.get<TransformComponent>(marker).position.x == 2, "Entity deletion OnDestroy did not see intact components");
            const auto raw = Add(engine, "RawDestroy", "Cleanup.lua"); frame();
            reg.destroy(raw);
            Check(reg.get<TransformComponent>(marker).position.x == 3, "Direct EnTT destruction removed API components before OnDestroy");
            const auto oldSceneRef = engine.GetScripts().GetRuntime().Reference(b);
            Check(engine.LoadScene("Scenes/LuaDemo.json", error), "Play scene switch was rejected"); waitScene();
            Check(engine.IsPlaying() && !engine.GetScripts().GetRuntime().Valid(oldSceneRef), "Scene swap retained old instances/references");
            Check(engine.GetScripts().GetStatus(Find(reg,"SpinnerSlow")) == "Running", "New scene scripts did not start after complete publication");
            engine.EndPlay();
            // Stop while another asynchronous scene read/publication is active.
            Check(engine.LoadScene("Scenes/LuaDemo.json", error), "Async restore fixture rejected");
            Check(!engine.BeginPlay(error), "Play started on a partial scene");
            Check(engine.RestorePlaySnapshot("Scenes/EditorPlaySnapshot.json", error), "Stop could not queue restoration behind pending scene");
            waitScene(); Check(!engine.IsPlaying(), "Queued restore restarted scripts");
            engine.Shutdown(); renderer.FlushCommandQueue();
            std::cout << "PASS: private globals/locals, optional callbacks, fixed ordering/edges, pause/re-enable, copied/batched events, cleanup, stale handles, syntax/missing/runtime errors, instruction hooks, scene generations, async restoration\n";
        }
        DestroyWindow(window); UnregisterClassW(cls.lpszClassName, instance);
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << std::endl; return 1; }
}
