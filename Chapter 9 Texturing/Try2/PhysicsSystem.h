#pragma once

#include "Commons.h"

class PhysicsSystem : public ISystem
{
public:
    void Update(entt::registry& reg, const FrameContext& context) override;

private:
    void Integrate(entt::registry& reg, float dt);
    void ResolveCollisions(entt::registry& reg);
    void ResolveCollision(
        TransformComponent& aTransform,
        RigidbodyComponent* aBody,
        const ColliderComponent& aCollider,
        TransformComponent& bTransform,
        RigidbodyComponent* bBody,
        const ColliderComponent& bCollider,
        const CollisionManifold& collision);
};
