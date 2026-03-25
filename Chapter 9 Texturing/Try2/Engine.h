#pragma once

#include "Commons.h"
#include "World.h"
#include "ResourceManager.h"

class IRenderAdapter
{
public:
    virtual ~IRenderAdapter() = default;

    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    virtual void DrawMesh(
        const MeshComponent mesh,
        TransformComponent& transform
    ) = 0;
};

class ConsoleRenderAdapter : public IRenderAdapter
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
    void DrawMesh(const MeshComponent mesh, TransformComponent& transform) override
    {
        std::cout << "Drawing mesh '" << mesh.meshID
            << "' at (" << transform.position.x << ", "
            << transform.position.y << ", "
            << transform.position.z << ")\n";
    }
};


class ISystem
{
public:
	virtual ~ISystem() = default;

	virtual void Update(entt::registry& reg, float dt) {}
};

class RenderSystem : public ISystem
{
public:
    RenderSystem(IRenderAdapter* adapter)
        : mAdapter(adapter) {}
	void Update(entt::registry& reg, float dt) override
	{
		mAdapter->BeginFrame();
        
		reg.view<TransformComponent, MeshComponent>().each([this](auto& transform, auto& mesh)
			{
                //auto& mesh = mResourceManager->GetMesh(MeshComponent.mesh);
                //auto& material = mResourceManager->GetMaterial(MeshComponent.material);

                // Передаём RenderAdapter
				mAdapter->DrawMesh(mesh, transform);
            });
		mAdapter->EndFrame();
	}
private:
    IRenderAdapter* mAdapter;
	ResourceManager* mResourceManager;
};

class Engine
{
private:
	World world;

	std::vector<std::unique_ptr<ISystem>> updateSystems;
	std::vector<std::unique_ptr<ISystem>> physicsSystems;
	std::vector<std::unique_ptr<ISystem>> renderSystems;

	ConsoleRenderAdapter mRenderAdapter;
	ResourceManager mResourceManager;
public:
	void Init();
	void Update(const GameTimer& gt);
	void PhysicsUpdate(float dt);
	void Draw(const GameTimer& gt);
};

