// Try2.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include <iostream>
#include <entt/entt.hpp>
#include "Application.h"
#include "ImGuiBridge.h"

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
    TestApp(HINSTANCE hInstance);
    /*TestApp(const TestApp& rhs) = delete;
    TestApp& operator=(const TestApp& rhs) = delete;
    ~TestApp();*/

    //virtual bool Initialize()override;

private:
    //virtual void Init()override;
    virtual void Update(const FrameContext& context)override;
    virtual void PhysicsUpdate(const FrameContext& context)override;
    virtual void Draw(const FrameContext& context)override;
};

TestApp::TestApp(HINSTANCE hInstance)
    : Application(hInstance)
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
	mEngine->Update(context);
}
void TestApp::Draw(const FrameContext& context)
{
	ZoneScopedN("Draw");
	mEngine->Draw(context);
}



int main()
{   

    try
    {
        ConfigureRuntimeDirectory();
        HINSTANCE hInstance = GetModuleHandle(nullptr);
        TestApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;

        ImGuiBridge::SetEngine(theApp.GetEngine());

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


