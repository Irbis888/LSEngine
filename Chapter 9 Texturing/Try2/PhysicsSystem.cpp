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
    ZoneScopedN("PhysicsCollisions");
    auto view = reg.view<TransformComponent, ColliderComponent>();
    std::vector<entt::entity> entities(view.begin(), view.end());
    std::vector<AABB> bounds;
    bounds.reserve(entities.size());
    for (entt::entity entity : entities)
    {
        const auto& transform = view.get<TransformComponent>(entity);
        bounds.push_back(MakeScaledAABB(transform.position, transform.scale, view.get<ColliderComponent>(entity)));
    }
    {
        ZoneScopedN("PhysicsBroadPhaseBuild");
        mBroadPhase.Build(bounds);
    }

    auto& stats = PhysicsStats::BroadPhase();
    stats = {};
    stats.possiblePairs = entities.empty() ? 0 : entities.size() * (entities.size() - 1) / 2;
    std::vector<size_t> candidates;
    for (size_t i = 0; i < entities.size(); ++i)
    {
        mBroadPhase.Query(i, i + 1, candidates);
        size_t cursor = 0;
        while (cursor < candidates.size())
        {
            const size_t j = candidates[cursor++];
            const entt::entity a = entities[i];
            const entt::entity b = entities[j];
            auto& aTransform = view.get<TransformComponent>(a);
            const auto& aCollider = view.get<ColliderComponent>(a);
            auto& bTransform = view.get<TransformComponent>(b);
            const auto& bCollider = view.get<ColliderComponent>(b);

            ++stats.narrowPhaseTests;
            const CollisionManifold collision = GetColliderCollision(mBroadPhase.Bounds(i), aCollider.type, mBroadPhase.Bounds(j), bCollider.type);
            if (!collision.colliding)
                continue;

            PhysicsStats::AddCollision();
            const glm::vec3 aPosition = aTransform.position;
            const glm::vec3 bPosition = bTransform.position;
            ResolveCollision(
                aTransform, reg.try_get<RigidbodyComponent>(a), aCollider,
                bTransform, reg.try_get<RigidbodyComponent>(b), bCollider, collision);

            // Keep the grid current as the sequential solver pushes bodies.
            // Sorting candidates and resuming after j preserves the old pair order.
            if (bTransform.position != bPosition)
                mBroadPhase.Update(j, MakeScaledAABB(bTransform.position, bTransform.scale, bCollider));
            if (aTransform.position != aPosition)
            {
                mBroadPhase.Update(i, MakeScaledAABB(aTransform.position, aTransform.scale, aCollider));
                mBroadPhase.Query(i, j + 1, candidates);
                cursor = 0;
            }
        }
    }
    stats.aabbTests = mBroadPhase.AabbTests();
    stats.gridCells = mBroadPhase.CellCount();
    stats.largeColliders = mBroadPhase.LargeCount();
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
