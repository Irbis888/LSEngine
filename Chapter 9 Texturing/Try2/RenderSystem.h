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
        mAdapter->SetTimeData(gt.TotalTime(), gt.DeltaTime());
        /*auto view = reg.view<CameraComponent>();

        for (auto e : view)
        {
			auto& camera = reg.get<TransformComponent>(e);
			camera.rotation.z += 2.0f * gt.DeltaTime(); // Rotate camera around Y-axis
        }*/

        reg.view<TransformComponent, CameraComponent>().each([this, &gt](auto& transform, auto& camera)
            {
				mAdapter->SetCamera(camera, transform);
            });
		mAdapter->UpdCB();
        reg.view<TransformComponent, MeshComponent>().each([this, &gt](auto& transform, auto& mesh)
            {
                mAdapter->SetTransform(transform);
				mAdapter->DrawMesh(mesh.meshID);
            });
    }
private:
    IRenderAdapter* mAdapter;
    ResourceManager* mResourceManager;
};

