#pragma once

#include "Commons.h"
#include "PhysicsBroadPhase.h"
#include "JobSystem.h"

class PhysicsSystem : public ISystem
{
public:
    explicit PhysicsSystem(JobSystem& jobs) : mJobs(jobs) {}
    struct SchedulingSettings
    {
        bool parallel = true;
        // Release measurements: dispatch overhead exceeds the saved work on
        // 256/512-body scenes; large batches benefit mainly in AABB calculation.
        uint32_t parallelThreshold = 8192;
        uint32_t rangeSize = 64;
    };
    void SetSchedulingSettings(const SchedulingSettings& settings);
    const SchedulingSettings& GetSchedulingSettings() const { return mScheduling; }
    const PhysicsStats::StepTimings& GetTimings() const { return mTimings; }
    // Synchronous step: all dispatched work is joined before returning. The
    // registry/component storage must not be mutated by other threads meanwhile.
    void Update(entt::registry& reg, const FrameContext& context) override;

private:
    struct BodyWork { TransformComponent* transform; RigidbodyComponent* body; };
    struct ColliderWork
    {
        TransformComponent* transform;
        const ColliderComponent* collider;
        RigidbodyComponent* body;
    };
    JobSystem& mJobs;
    SchedulingSettings mScheduling;
    PhysicsStats::StepTimings mTimings;
    std::vector<BodyWork> mBodies;
    std::vector<ColliderWork> mColliders;
    std::vector<AABB> mBounds;
    std::vector<size_t> mCandidates;
    PhysicsBroadPhase mBroadPhase;
    void Prepare(entt::registry& reg);
    bool RunRanges(size_t count, JobSystem::RangeJob work);
    void IntegrateRange(uint32_t begin, uint32_t end, float dt);
    void BoundsRange(uint32_t begin, uint32_t end);
    void ResolveCollisions();
    void ResolveCollision(
        TransformComponent& aTransform,
        RigidbodyComponent* aBody,
        const ColliderComponent& aCollider,
        TransformComponent& bTransform,
        RigidbodyComponent* bBody,
        const ColliderComponent& bCollider,
        const CollisionManifold& collision);
};
