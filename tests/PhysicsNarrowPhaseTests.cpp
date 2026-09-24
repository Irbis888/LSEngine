#include "PhysicsSystem.h"
#include <cmath>
#include <stdexcept>

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static bool Near(glm::vec3 a, glm::vec3 b)
{
    return glm::length(a - b) < 1e-5f;
}

static void Contact(const AABB& a, ColliderType aType, const AABB& b, ColliderType bType,
                    glm::vec3 normal, float depth)
{
    const auto hit = GetColliderCollision(a, aType, b, bType);
    Check(hit.colliding, "Missing narrow-phase contact");
    Check(Near(hit.normal, normal), "Wrong contact normal");
    Check(std::abs(hit.penetrationDepth - depth) < 1e-5f, "Wrong penetration depth");
    const auto reverse = GetColliderCollision(b, bType, a, aType);
    Check(reverse.colliding && Near(reverse.normal, -normal), "Swapped pair has wrong normal");
    Check(std::abs(reverse.penetrationDepth - depth) < 1e-5f, "Swapped pair has wrong depth");
    // Moving A opposite the reported normal must remove the penetration.
    AABB separated = a;
    separated.center -= normal * (depth + 1e-4f);
    Check(!GetColliderCollision(separated, aType, b, bType).colliding, "Contact normal pushes A into B");
}

void TestNarrowPhase()
{
    const auto sphere = ColliderType::Sphere;
    const auto box = ColliderType::AABB;
    const AABB unitBox{{0, 0, 0}, {1, 1, 1}};
    Contact(unitBox, box, {{1.5f, 0, 0}, {1, 1, 1}}, box, {1, 0, 0}, 0.5f);
    Contact(unitBox, sphere, {{1.5f, 0, 0}, {1, 1, 1}}, sphere, {1, 0, 0}, 0.5f);

    const AABB diagonalSphere{{1.5f, 1.5f, 0}, {1, 1, 1}};
    Check(IntersectsAABB(unitBox, diagonalSphere), "Test must overlap broad-phase bounds");
    Check(!GetColliderCollision(unitBox, sphere, diagonalSphere, sphere).colliding, "Sphere corners collide like boxes");
    Check(!GetSphereCollision({0, 0, 0}, 1, {2, 0, 0}, 1).colliding, "Tangency is not penetration");
    const auto coincident = GetSphereCollision({0, 0, 0}, 1, {0, 0, 0}, 0.5f);
    Check(coincident.colliding && Near(coincident.normal, {1, 0, 0}) &&
          coincident.penetrationDepth == 1.5f, "Coincident spheres need a finite fallback");

    Contact({{1.4f, 0, 0}, {0.5f, 0.5f, 0.5f}}, sphere, unitBox, box, {-1, 0, 0}, 0.1f);
    Contact({{1.3f, 1.4f, 0}, {0.6f, 0.6f, 0.6f}}, sphere, unitBox, box, {-0.6f, -0.8f, 0}, 0.1f);
    Contact({{1.2f, 1.2f, 1.2f}, {0.5f, 0.5f, 0.5f}}, sphere, unitBox, box,
            glm::normalize(glm::vec3(-1)), 0.5f - std::sqrt(0.12f));
    Check(!GetSphereAABBCollision({1.4f, 1.4f, 1.4f}, 0.5f, unitBox).colliding, "False contact at box corner");
    Check(!GetSphereAABBCollision({1.5f, 0, 0}, 0.5f, unitBox).colliding, "Face tangency");
    Contact({{0.8f, 0, 0}, {0.5f, 0.5f, 0.5f}}, sphere, unitBox, box, {-1, 0, 0}, 0.7f);
    Contact({{0, 0, 0}, {0.5f, 0.5f, 0.5f}}, sphere, unitBox, box, {-1, 0, 0}, 1.5f);
    Contact({{1, 0, 0}, {0.5f, 0.5f, 0.5f}}, sphere, unitBox, box, {-1, 0, 0}, 0.5f);
    Contact({{-0.8f, 0, 0}, {0.5f, 0.5f, 0.5f}}, sphere, unitBox, box, {1, 0, 0}, 0.7f);

    ColliderComponent scaledSphere;
    scaledSphere.type = sphere;
    scaledSphere.offset = {0.25f, 0, 0};
    const auto scaled = MakeScaledAABB({10, 0, 0}, {-2, 1, 3}, scaledSphere);
    Check(Near(scaled.center, {10.5f, 0, 0}) && Near(scaled.halfExtents, {1.5f, 1.5f, 1.5f}),
          "Scaled sphere must use the same center/radius in both phases");
    Contact(scaled, sphere, {{13, 0, 0}, {1.5f, 1.5f, 1.5f}}, sphere, {1, 0, 0}, 0.5f);

    // End-to-end: the broad-phase may return a pair but the solver must not
    // separate spheres whose enclosing boxes alone overlap.
    entt::registry reg;
    auto addSphere = [&](glm::vec3 position, glm::vec3 velocity)
    {
        auto entity = reg.create();
        reg.emplace<TransformComponent>(entity, position, glm::vec3(0), glm::vec3(1));
        auto& body = reg.emplace<RigidbodyComponent>(entity);
        body.useGravity = false;
        body.velocity = velocity;
        auto& collider = reg.emplace<ColliderComponent>(entity);
        collider.type = sphere;
        collider.radius = 0.5f;
        collider.restitution = 1.0f;
        collider.friction = 0.0f;
        return entity;
    };
    auto a = addSphere({0, 0, 0}, {0, 0, 0});
    auto b = addSphere({0.8f, 0.8f, 0}, {0, 0, 0});
    PhysicsSystem system;
    GameTimer timer;
    FrameContext context{timer, {}, 1.0f / 60};
    system.Update(reg, context);
    Check(PhysicsStats::BroadPhase().narrowPhaseTests == 1 && PhysicsStats::GetFrameCollisionCount() == 0,
          "Broad-phase false positive reached collision response");
    Check(Near(reg.get<TransformComponent>(a).position, {0, 0, 0}) &&
          Near(reg.get<TransformComponent>(b).position, {0.8f, 0.8f, 0}), "Separated spheres moved");

    reg.get<TransformComponent>(a).position = {-0.4f, 0, 0};
    reg.get<TransformComponent>(b).position = {0.4f, 0, 0};
    reg.get<RigidbodyComponent>(a).velocity = {1, 0, 0};
    reg.get<RigidbodyComponent>(b).velocity = {-1, 0, 0};
    system.Update(reg, context);
    Check(Near(reg.get<RigidbodyComponent>(a).velocity, {-1, 0, 0}) &&
          Near(reg.get<RigidbodyComponent>(b).velocity, {1, 0, 0}), "Sphere impulse response");
    Check(Near(reg.get<TransformComponent>(a).position, {-0.5f, 0, 0}) &&
          Near(reg.get<TransformComponent>(b).position, {0.5f, 0, 0}), "Sphere separation");

    reg.get<TransformComponent>(a).position = {0, 0, 0};
    reg.get<TransformComponent>(b).position = {0.1f, 0, 0};
    reg.get<RigidbodyComponent>(a).velocity = reg.get<RigidbodyComponent>(b).velocity = glm::vec3(0);
    reg.get<ColliderComponent>(b).isTrigger = true;
    system.Update(reg, context);
    Check(PhysicsStats::GetFrameCollisionCount() == 1 &&
          Near(reg.get<TransformComponent>(a).position, {0, 0, 0}) &&
          Near(reg.get<TransformComponent>(b).position, {0.1f, 0, 0}), "Sphere trigger response");

    // Sphere on an AABB floor, both entity orders (both dispatch branches).
    for (int order = 0; order < 2; ++order)
    {
        reg.clear();
        entt::entity ball, floor;
        auto addFloor = [&]
        {
            auto e = reg.create();
            reg.emplace<TransformComponent>(e, glm::vec3(0), glm::vec3(0), glm::vec3(1));
            reg.emplace<ColliderComponent>(e).halfExtents = {20, 0.1f, 20};
            return e;
        };
        if (order == 0) { floor = addFloor(); ball = addSphere({0, 2, 0}, {0, 0, 0}); }
        else { ball = addSphere({0, 2, 0}, {0, 0, 0}); floor = addFloor(); }
        reg.get<RigidbodyComponent>(ball).useGravity = true;
        reg.get<ColliderComponent>(ball).restitution = 0;
        for (int step = 0; step < 180; ++step) system.Update(reg, context);
        Check(std::abs(reg.get<TransformComponent>(ball).position.y - 0.6f) < 1e-4f, "Sphere fell through AABB floor");
    }
}

