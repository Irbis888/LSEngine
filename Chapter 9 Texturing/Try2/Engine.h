#pragma once

#include "Commons.h"
#include "World.h"
#include "ResourceManager.h"

class Engine
{
private:
	World world;

	std::vector<std::unique_ptr<ISystem>> updateSystems;
	std::vector<std::unique_ptr<ISystem>> physicsSystems;
	std::vector<std::unique_ptr<ISystem>> renderSystems;

    IRenderAdapter* mRenderAdapter;
	ResourceManager mResourceManager;
public:
    Engine(IRenderAdapter* renderer)
        : mRenderAdapter(renderer) {
    };

	void Init();
	void Update(const GameTimer& gt);
	void PhysicsUpdate(float dt);
	void Draw(const GameTimer& gt);
};

/*class ConsoleRenderAdapter : public IRenderAdapter
{
public:
    void BeginFrame() override
    {
        std::cout << "=== Begin Frame ===\n";
    }
    void EndFrame() override
    {
        std::cout << "=== End Frame ===\n\n";
    }
    void DrawIndexed(const MeshComponent mesh, TransformComponent& transform) override
    {
        std::cout << "Drawing mesh '" << mesh.meshID
            << "' at (" << transform.position.x << ", "
            << transform.position.y << ", "
            << transform.position.z << ")\n";
    }
};*/