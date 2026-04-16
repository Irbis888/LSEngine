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
    void Update(entt::registry& reg, const GameTimer& gt) override
    {
        reg.view<TransformComponent, MeshComponent>().each([this, &gt](auto& transform, auto& mesh)
            {
				mAdapter->SetTimeData(gt.TotalTime(), gt.DeltaTime());
                //auto& mesh = mResourceManager->GetMesh(MeshComponent.mesh);
                //auto& material = mResourceManager->GetMaterial(MeshComponent.material);

                // Передаём RenderAdapter
                mAdapter->SetTransform(transform);
				mAdapter->DrawMesh(mesh.meshID);
            });
    }
private:
    IRenderAdapter* mAdapter;
    ResourceManager* mResourceManager;
};

