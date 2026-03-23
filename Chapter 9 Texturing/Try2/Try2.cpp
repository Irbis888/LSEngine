// Try2.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include <iostream>
#include <entt/entt.hpp>
#include "Application.h"
#include <string>

struct Position { float x, y; };
struct Velocity { float x, y; };

struct DxException
{
    int ErrorCode = 0;
    std::wstring Message = L"Stub";
};

class TestApp : public Application
{
public:
    TestApp(HINSTANCE hInstance);
    /*TestApp(const TestApp& rhs) = delete;
    TestApp& operator=(const TestApp& rhs) = delete;
    ~TestApp();*/

    //virtual bool Initialize()override;

private:
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;
};

TestApp::TestApp(HINSTANCE hInstance)
    : Application(hInstance)
{
}
void TestApp::Update(const GameTimer& gt)
{
    std::cout << "Update: " << gt.DeltaTime() << " seconds\n";
}

void TestApp::Draw(const GameTimer& gt)
{
    std::cout << "Draw: " << gt.DeltaTime() << " seconds\n";
}



int main()
{
    entt::registry registry;

    auto e1 = registry.create();
    registry.emplace<Position>(e1, 1.0f, 2.0f);
    registry.emplace<Velocity>(e1, 3.0f, 4.0f);

    auto e2 = registry.create();
    registry.emplace<Position>(e2, 5.0f, 6.0f);
    registry.emplace<Velocity>(e2, 7.0f, 8.0f);

    auto view = registry.view<Position, Velocity>();
    std::cout << "Hello World!\n";

    view.each([&](const entt::entity ent, const Position &p, const Velocity &v) {
        std::cout << "entity " << static_cast<unsigned int>(ent)
                  << ": Position(" << p.x << ", " << p.y << ")"
                  << " Velocity(" << v.x << ", " << v.y << ")\n";
    });
    
    for (auto e : view)
    {
        auto& p = view.get<Position>(e);
        auto& v = view.get<Velocity>(e);
        std::cout << "entity " << static_cast<unsigned int>(e)
            << ": Position(" << p.x << ", " << p.y << ")"
            << " Velocity(" << v.x << ", " << v.y << ")\n";
    }

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
        MessageBox(nullptr, e.Message.c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}


