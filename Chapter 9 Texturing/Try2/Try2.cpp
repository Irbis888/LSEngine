// Try2.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include <iostream>
#include <entt/entt.hpp>
#include "Application.h"
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
    virtual void Update(const GameTimer& gt)override;
    virtual void PhysicsUpdate(float dt)override;
    virtual void Draw(const GameTimer& gt)override;
};

TestApp::TestApp(HINSTANCE hInstance)
    : Application(hInstance)
{
}
void TestApp::PhysicsUpdate(float dt)
{
    
    //std::cout << "Physics tick: " << dt << " seconds\n";
    mEngine->PhysicsUpdate(dt);
}
void TestApp::Update(const GameTimer& gt)
{
	mEngine->Update(gt);
    //std::cout << "Update: " << gt.DeltaTime() << " seconds\n";
}
void TestApp::Draw(const GameTimer& gt)
{
    //std::cout << "Draw: " << gt.TotalTime() << " seconds\n";
	mEngine->Draw(gt);
}



int main()
{   

    try
    {
        HINSTANCE hInstance = GetModuleHandle(nullptr);
        TestApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}


