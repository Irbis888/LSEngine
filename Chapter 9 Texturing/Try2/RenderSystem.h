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
    void Update(entt::registry& reg, const FrameContext& context) override
    {
        mAdapter->SetTimeData(context.timer.TotalTime(), context.timer.DeltaTime());
        reg.view<TransformComponent, CameraComponent>().each([this, &context](auto& transform, auto& camera)
            {
				mAdapter->SetCamera(camera, transform);
				
            });
		mAdapter->UpdCB();
        reg.view<TransformComponent, MeshComponent>().each([this, &context](auto& transform, auto& mesh)
            {
                mAdapter->SetTransform(transform);
				mAdapter->DrawMesh(mesh.meshID);
            });
    }
private:
    IRenderAdapter* mAdapter;
    ResourceManager* mResourceManager;
};

