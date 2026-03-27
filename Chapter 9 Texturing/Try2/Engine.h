#pragma once

#include "Commons.h"
#include "World.h"
#include "ResourceManager.h"


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




class RenderSystem : public ISystem
{
public:
    RenderSystem(IRenderAdapter* adapter)
        : mAdapter(adapter) {}
	void Update(entt::registry& reg, float dt) override
	{
		reg.view<TransformComponent, MeshComponent>().each([this](auto& transform, auto& mesh)
			{
                //auto& mesh = mResourceManager->GetMesh(MeshComponent.mesh);
                //auto& material = mResourceManager->GetMaterial(MeshComponent.material);

                // Передаём RenderAdapter
				mAdapter->SetTransform(glm::mat4(1.0f));
				mAdapter->DrawIndexed(mesh.meshID, 0, 0);
            });
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

