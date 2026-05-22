// Try2.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include <iostream>
#include <entt/entt.hpp>
#include "Application.h"
#include "ImGuiBridge.h"
#include <string>

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
    mEngine->PhysicsUpdate(context);
}
void TestApp::Update(const FrameContext& context)
{
	mEngine->Update(context);
}
void TestApp::Draw(const FrameContext& context)
{
	mEngine->Draw(context);
}



int main()
{   

    try
    {
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
        return 0;
    }
}


