#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/glm.hpp>

namespace Physics
{
    inline const glm::vec3 DefaultGravity(0.0f, -9.81f, 0.0f);
    constexpr float MinMass = 0.0001f;
}

enum class RigidbodyType
{
    Static,
    Dynamic,
    Kinematic
};

enum class ColliderType
{
    AABB,
    Sphere
};

struct RigidbodyComponent
{
    RigidbodyType type = RigidbodyType::Dynamic;
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 acceleration = glm::vec3(0.0f);
    float mass = 1.0f;
    bool useGravity = true;

    bool IsStatic() const
    {
        return type == RigidbodyType::Static;
    }

    bool IsDynamic() const
    {
        return type == RigidbodyType::Dynamic;
    }

    float InverseMass() const
    {
        if (type != RigidbodyType::Dynamic)
            return 0.0f;

        return 1.0f / (std::max)(mass, Physics::MinMass);
    }
};

struct ColliderComponent
{
    ColliderType type = ColliderType::AABB;
    glm::vec3 offset = glm::vec3(0.0f);
    glm::vec3 halfExtents = glm::vec3(0.5f);
    float radius = 0.5f;
    bool isTrigger = false;
    float restitution = 0.0f;
    float friction = 0.5f;
};

struct AABB
{
    glm::vec3 center = glm::vec3(0.0f);
    glm::vec3 halfExtents = glm::vec3(0.5f);

    glm::vec3 Min() const
    {
        return center - halfExtents;
    }

    glm::vec3 Max() const
    {
        return center + halfExtents;
    }
};

struct CollisionManifold
{
    bool colliding = false;
    glm::vec3 normal = glm::vec3(0.0f);
    float penetrationDepth = 0.0f;
};

inline AABB MakeAABB(const glm::vec3& position, const ColliderComponent& collider)
{
    const glm::vec3 halfExtents =
        collider.type == ColliderType::Sphere
            ? glm::vec3(collider.radius)
            : collider.halfExtents;

    return AABB{ position + collider.offset, halfExtents };
}

inline AABB MakeScaledAABB(
    const glm::vec3& position,
    const glm::vec3& scale,
    const ColliderComponent& collider)
{
    const glm::vec3 absScale = glm::abs(scale);

    if (collider.type == ColliderType::Sphere)
    {
        const float maxScale = (std::max)((std::max)(absScale.x, absScale.y), absScale.z);
        return AABB{ position + collider.offset * absScale, glm::vec3(collider.radius * maxScale) };
    }

    return AABB{ position + collider.offset * absScale, collider.halfExtents * absScale };
}

inline bool IntersectsAABB(const AABB& a, const AABB& b)
{
    const glm::vec3 aMin = a.Min();
    const glm::vec3 aMax = a.Max();
    const glm::vec3 bMin = b.Min();
    const glm::vec3 bMax = b.Max();

    return !(aMax.x < bMin.x || aMin.x > bMax.x ||
             aMax.y < bMin.y || aMin.y > bMax.y ||
             aMax.z < bMin.z || aMin.z > bMax.z);
}

inline CollisionManifold GetAABBCollision(const AABB& a, const AABB& b)
{
    CollisionManifold result;

    const glm::vec3 delta = b.center - a.center;
    const glm::vec3 overlap = (a.halfExtents + b.halfExtents) - glm::abs(delta);

    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f)
        return result;

    result.colliding = true;

    if (overlap.x <= overlap.y && overlap.x <= overlap.z)
    {
        result.penetrationDepth = overlap.x;
        result.normal = glm::vec3(delta.x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
    }
    else if (overlap.y <= overlap.z)
    {
        result.penetrationDepth = overlap.y;
        result.normal = glm::vec3(0.0f, delta.y < 0.0f ? -1.0f : 1.0f, 0.0f);
    }
    else
    {
        result.penetrationDepth = overlap.z;
        result.normal = glm::vec3(0.0f, 0.0f, delta.z < 0.0f ? -1.0f : 1.0f);
    }

    return result;
}

inline bool IntersectsSphere(
    const glm::vec3& aCenter,
    float aRadius,
    const glm::vec3& bCenter,
    float bRadius)
{
    const float radiusSum = aRadius + bRadius;
    const glm::vec3 delta = bCenter - aCenter;

    return glm::dot(delta, delta) <= radiusSum * radiusSum;
}

// Contact normals follow the solver convention: from A toward B.
// Exact tangency is not penetration, matching GetAABBCollision.
inline CollisionManifold GetSphereCollision(
    const glm::vec3& aCenter, float aRadius,
    const glm::vec3& bCenter, float bRadius)
{
    CollisionManifold result;
    const glm::vec3 delta = bCenter - aCenter;
    const float radiusSum = aRadius + bRadius;
    const float distanceSquared = glm::dot(delta, delta);
    if (radiusSum <= 0.0f || distanceSquared >= radiusSum * radiusSum)
        return result;

    const float distance = std::sqrt(distanceSquared);
    result.colliding = true;
    // Coincident centers have no unique direction; use a stable fallback.
    result.normal = distance > 0.0f ? delta / distance : glm::vec3(1, 0, 0);
    result.penetrationDepth = radiusSum - distance;
    return result;
}

inline CollisionManifold GetSphereAABBCollision(
    const glm::vec3& sphereCenter, float radius, const AABB& box)
{
    CollisionManifold result;
    const glm::vec3 closest = glm::clamp(sphereCenter, box.Min(), box.Max());
    const glm::vec3 towardBox = closest - sphereCenter;
    const float distanceSquared = glm::dot(towardBox, towardBox);

    if (distanceSquared > 0.0f)
    {
        if (radius <= 0.0f || distanceSquared >= radius * radius)
            return result;

        const float distance = std::sqrt(distanceSquared);
        result.colliding = true;
        result.normal = towardBox / distance;
        result.penetrationDepth = radius - distance;
        return result;
    }

    // The center is inside/on the box. Push the sphere out through the nearest
    // face, including its radius. Negate outward because the solver moves A - N.
    const glm::vec3 localCenter = sphereCenter - box.center;
    const glm::vec3 faceDistance = box.halfExtents - glm::abs(localCenter);
    int axis = 0;
    if (faceDistance.y < faceDistance[axis]) axis = 1;
    if (faceDistance.z < faceDistance[axis]) axis = 2;
    result.penetrationDepth = radius + faceDistance[axis];
    if (result.penetrationDepth <= 0.0f)
        return result;

    result.colliding = true;
    result.normal = glm::vec3(0);
    result.normal[axis] = localCenter[axis] < 0.0f ? 1.0f : -1.0f;
    return result;
}

// Bounds come from MakeScaledAABB. A sphere's bounds have equal half-extents,
// equal to its world radius (largest absolute scale component).
inline CollisionManifold GetColliderCollision(
    const AABB& aBounds, ColliderType aType,
    const AABB& bBounds, ColliderType bType)
{
    if (aType == ColliderType::Sphere && bType == ColliderType::Sphere)
        return GetSphereCollision(aBounds.center, aBounds.halfExtents.x,
                                  bBounds.center, bBounds.halfExtents.x);
    if (aType == ColliderType::Sphere)
        return GetSphereAABBCollision(aBounds.center, aBounds.halfExtents.x, bBounds);
    if (bType == ColliderType::Sphere)
    {
        CollisionManifold result = GetSphereAABBCollision(bBounds.center, bBounds.halfExtents.x, aBounds);
        result.normal = -result.normal;
        return result;
    }
    return GetAABBCollision(aBounds, bBounds);
}


namespace PhysicsStats
{
    // All counters describe the last completed physics step, not a render frame.
    struct BroadPhaseStats
    {
        size_t possiblePairs = 0;
        size_t aabbTests = 0;
        size_t narrowPhaseTests = 0;
        size_t gridCells = 0;
        size_t largeColliders = 0;
    };

    inline BroadPhaseStats& BroadPhase()
    {
        static BroadPhaseStats stats;
        return stats;
    }
    inline int& FrameCollisionCount()
    {
        static int count = 0;
        return count;
    }

    inline void ResetFrameCollisionCount()
    {
        FrameCollisionCount() = 0;
    }

    inline void AddCollision()
    {
        ++FrameCollisionCount();
    }

    inline int GetFrameCollisionCount()
    {
        return FrameCollisionCount();
    }
}
