#include "PhysicsSystem.h"

#include <vector>

#include <glm/geometric.hpp>

void PhysicsSystem::Update(entt::registry& reg, const FrameContext& context)
{
    PhysicsStats::ResetFrameCollisionCount();

    const float dt = context.physDT > 0.0f ? context.physDT : context.timer.DeltaTime();

    Integrate(reg, dt);
    ResolveCollisions(reg);
}

void PhysicsSystem::Integrate(entt::registry& reg, float dt)
{
    auto view = reg.view<TransformComponent, RigidbodyComponent>();

    for (entt::entity entity : view)
    {
        auto& transform = view.get<TransformComponent>(entity);
        auto& body = view.get<RigidbodyComponent>(entity);

        if (!body.IsDynamic())
            continue;

        if (body.useGravity)
        {
            body.velocity += Physics::DefaultGravity * dt;
        }

        body.velocity += body.acceleration * dt;
        transform.position += body.velocity * dt;
    }
}

void PhysicsSystem::ResolveCollisions(entt::registry& reg)
{
    auto view = reg.view<TransformComponent, ColliderComponent>();
    std::vector<entt::entity> entities(view.begin(), view.end());

    for (size_t i = 0; i < entities.size(); ++i)
    {
        for (size_t j = i + 1; j < entities.size(); ++j)
        {
            entt::entity a = entities[i];
            entt::entity b = entities[j];

            auto& aTransform = view.get<TransformComponent>(a);
            const auto& aCollider = view.get<ColliderComponent>(a);
            auto& bTransform = view.get<TransformComponent>(b);
            const auto& bCollider = view.get<ColliderComponent>(b);

            const AABB aBounds = MakeScaledAABB(aTransform.position, aTransform.scale, aCollider);
            const AABB bBounds = MakeScaledAABB(bTransform.position, bTransform.scale, bCollider);
            const CollisionManifold collision = GetAABBCollision(aBounds, bBounds);

            if (!collision.colliding)
                continue;

            PhysicsStats::AddCollision();

            RigidbodyComponent* aBody = reg.try_get<RigidbodyComponent>(a);
            RigidbodyComponent* bBody = reg.try_get<RigidbodyComponent>(b);

            ResolveCollision(
                aTransform,
                aBody,
                aCollider,
                bTransform,
                bBody,
                bCollider,
                collision);
        }
    }
}

void PhysicsSystem::ResolveCollision(
    TransformComponent& aTransform,
    RigidbodyComponent* aBody,
    const ColliderComponent& aCollider,
    TransformComponent& bTransform,
    RigidbodyComponent* bBody,
    const ColliderComponent& bCollider,
    const CollisionManifold& collision)
{
    if (aCollider.isTrigger || bCollider.isTrigger)
        return;

    const float aInvMass = aBody ? aBody->InverseMass() : 0.0f;
    const float bInvMass = bBody ? bBody->InverseMass() : 0.0f;
    const float invMassSum = aInvMass + bInvMass;

    if (invMassSum <= 0.0f)
        return;

    const glm::vec3 correction = collision.normal * collision.penetrationDepth;
    aTransform.position -= correction * (aInvMass / invMassSum);
    bTransform.position += correction * (bInvMass / invMassSum);

    glm::vec3 aVelocity = aBody ? aBody->velocity : glm::vec3(0.0f);
    glm::vec3 bVelocity = bBody ? bBody->velocity : glm::vec3(0.0f);
    const glm::vec3 relativeVelocity = bVelocity - aVelocity;
    const float velocityAlongNormal = glm::dot(relativeVelocity, collision.normal);

    if (velocityAlongNormal > 0.0f)
        return;

    const float restitution = (std::min)(aCollider.restitution, bCollider.restitution);
    const float impulseMagnitude = -(1.0f + restitution) * velocityAlongNormal / invMassSum;
    const glm::vec3 impulse = impulseMagnitude * collision.normal;

    if (aBody && aBody->IsDynamic())
        aBody->velocity -= impulse * aInvMass;

    if (bBody && bBody->IsDynamic())
        bBody->velocity += impulse * bInvMass;

    const float friction = (std::max)(0.0f, (std::min)(aCollider.friction, bCollider.friction));

    auto applyFriction = [&](RigidbodyComponent* body)
        {
            if (!body || !body->IsDynamic())
                return;

            const float normalSpeed = glm::dot(body->velocity, collision.normal);
            const glm::vec3 normalVelocity = normalSpeed * collision.normal;
            glm::vec3 tangentVelocity = body->velocity - normalVelocity;

            tangentVelocity *= (std::max)(0.0f, 1.0f - friction);

            if (glm::dot(tangentVelocity, tangentVelocity) < 0.0001f)
                tangentVelocity = glm::vec3(0.0f);

            body->velocity = normalVelocity + tangentVelocity;
        };

    applyFriction(aBody);
    applyFriction(bBody);
}
