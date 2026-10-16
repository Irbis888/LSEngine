#include "PhysicsSystem.h"

#include <vector>
#include <chrono>
#include <limits>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace
{
    using Clock = std::chrono::steady_clock;
    double ElapsedMs(Clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
}

void PhysicsSystem::SetSchedulingSettings(const SchedulingSettings& settings)
{
    if (!settings.parallelThreshold || !settings.rangeSize)
        throw std::invalid_argument("Physics threshold and range size must be positive");
    mScheduling = settings;
}

void PhysicsSystem::Update(entt::registry& reg, const FrameContext& context)
{
    ZoneScopedN("PhysicsStep");
    const auto start = Clock::now();
    mTimings = {};
    PhysicsStats::ResetFrameCollisionCount();
    const float dt = context.physDT > 0.0f ? context.physDT : context.timer.DeltaTime();
    Prepare(reg);
    mTimings.prepareMs = ElapsedMs(start);
    {
        ZoneScopedN("PhysicsIntegration");
        const auto phase = Clock::now();
        mTimings.parallelIntegration = RunRanges(mBodies.size(),
            [this, dt](uint32_t begin, uint32_t end) { IntegrateRange(begin, end, dt); });
        mTimings.integrateMs = ElapsedMs(phase);
    }
    {
        ZoneScopedN("PhysicsInitialAABB");
        const auto phase = Clock::now();
        mTimings.parallelAabb = RunRanges(mColliders.size(),
            [this](uint32_t begin, uint32_t end) { BoundsRange(begin, end); });
        mTimings.aabbMs = ElapsedMs(phase);
    }
    {
        ZoneScopedN("PhysicsBroadPhaseBuild");
        const auto phase = Clock::now();
        mBroadPhase.Build(mBounds);
        mTimings.broadPhaseMs = ElapsedMs(phase);
    }
    const auto solver = Clock::now();
    ResolveCollisions();
    mTimings.solverMs = ElapsedMs(solver);
    mTimings.totalMs = ElapsedMs(start);
    PhysicsStats::Timings() = mTimings;
    TracyPlot("Physics prepare ms", mTimings.prepareMs);
    TracyPlot("Physics integration ms", mTimings.integrateMs);
    TracyPlot("Physics AABB ms", mTimings.aabbMs);
    TracyPlot("Physics broad phase ms", mTimings.broadPhaseMs);
    TracyPlot("Physics solver ms", mTimings.solverMs);
    TracyPlot("Physics total ms", mTimings.totalMs);
}

void PhysicsSystem::Prepare(entt::registry& reg)
{
    ZoneScopedN("PhysicsPrepare");
    mBodies.clear();
    mColliders.clear();
    auto bodies = reg.view<TransformComponent, RigidbodyComponent>();
    mBodies.reserve(bodies.size_hint());
    for (auto entity : bodies)
        mBodies.push_back({ &bodies.get<TransformComponent>(entity), &bodies.get<RigidbodyComponent>(entity) });
    auto colliders = reg.view<TransformComponent, ColliderComponent>();
    mColliders.reserve(colliders.size_hint());
    // Preserve the old collider view order: the sequential solver depends on it.
    for (auto entity : colliders)
        mColliders.push_back({ &colliders.get<TransformComponent>(entity),
            &colliders.get<ColliderComponent>(entity), reg.try_get<RigidbodyComponent>(entity) });
    mBounds.resize(mColliders.size());
    mTimings.bodies = mBodies.size();
    mTimings.colliders = mColliders.size();
}

bool PhysicsSystem::RunRanges(size_t count, JobSystem::RangeJob work)
{
    if (count > (std::numeric_limits<uint32_t>::max)())
        throw std::length_error("Physics work exceeds Dispatch capacity");
    if (mScheduling.parallel && count >= mScheduling.parallelThreshold && mJobs.IsInitialized())
    {
        JobSystem::TaskHandle task;
        {
            ZoneScopedN("PhysicsDispatch");
            ZoneValue(count);
            task = mJobs.Dispatch(static_cast<uint32_t>(count), std::move(work),
                mScheduling.rangeSize, JobSystem::Priority::High);
        }
        {
            // Range zones on this thread can nest here while Wait helps workers.
            ZoneScopedN("PhysicsWaitHighPriority");
            mJobs.WaitHighPriority(task);
        }
        return true;
    }
    ZoneScopedN("PhysicsSerialRange");
    ZoneValue(count);
    work(0, static_cast<uint32_t>(count));
    return false;
}

void PhysicsSystem::IntegrateRange(uint32_t begin, uint32_t end, float dt)
{
    ZoneScopedN("PhysicsIntegrateRange");
    ZoneValue(end - begin);
    for (uint32_t i = begin; i < end; ++i)
    {
        auto& transform = *mBodies[i].transform;
        auto& body = *mBodies[i].body;
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

void PhysicsSystem::BoundsRange(uint32_t begin, uint32_t end)
{
    ZoneScopedN("PhysicsBoundsRange");
    ZoneValue(end - begin);
    for (uint32_t i = begin; i < end; ++i)
    {
        const auto& transform = *mColliders[i].transform;
        mBounds[i] = MakeScaledAABB(transform.position, transform.scale, *mColliders[i].collider);
    }
}

void PhysicsSystem::ResolveCollisions()
{
    ZoneScopedN("PhysicsCollisions");
    auto& stats = PhysicsStats::BroadPhase();
    stats = {};
    stats.possiblePairs = mColliders.empty() ? 0 : mColliders.size() * (mColliders.size() - 1) / 2;
    auto& candidates = mCandidates;
    for (size_t i = 0; i < mColliders.size(); ++i)
    {
        mBroadPhase.Query(i, i + 1, candidates);
        size_t cursor = 0;
        while (cursor < candidates.size())
        {
            const size_t j = candidates[cursor++];
            auto& aTransform = *mColliders[i].transform;
            const auto& aCollider = *mColliders[i].collider;
            auto& bTransform = *mColliders[j].transform;
            const auto& bCollider = *mColliders[j].collider;

            ++stats.narrowPhaseTests;
            const CollisionManifold collision = GetColliderCollision(mBroadPhase.Bounds(i), aCollider.type, mBroadPhase.Bounds(j), bCollider.type);
            if (!collision.colliding)
                continue;

            PhysicsStats::AddCollision();
            const glm::vec3 aPosition = aTransform.position;
            const glm::vec3 bPosition = bTransform.position;
            ResolveCollision(
                aTransform, mColliders[i].body, aCollider,
                bTransform, mColliders[j].body, bCollider, collision);

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
