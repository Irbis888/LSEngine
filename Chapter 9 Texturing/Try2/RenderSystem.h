#pragma once
#include "Commons.h"
#include "ResourceManager.h"
#include "World.h"


class RenderSystem : public ISystem
{
public:
    RenderSystem(IRenderAdapter* adapter)
        : mAdapter(adapter) {
    }
    void Update(entt::registry& reg, float dt) override
    {
        reg.view<TransformComponent, MeshComponent>().each([this](auto& transform, auto& mesh)
            {
                //auto& mesh = mResourceManager->GetMesh(MeshComponent.mesh);
                //auto& material = mResourceManager->GetMaterial(MeshComponent.material);

                // Передаём RenderAdapter
                mAdapter->SetTransform(transform);
                mAdapter->DrawIndexed(mesh.meshID, 0, 0);
            });
    }
private:
    IRenderAdapter* mAdapter;
    ResourceManager* mResourceManager;
};

