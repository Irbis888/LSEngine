#include "PhysicsSystem.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

void TestNarrowPhase();

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void CheckQueries(PhysicsBroadPhase& grid, const std::vector<AABB>& boxes)
{
    std::vector<size_t> actual;
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        grid.Query(i, i + 1, actual);
        std::vector<size_t> expected;
        for (size_t j = i + 1; j < boxes.size(); ++j)
            if (IntersectsAABB(boxes[i], boxes[j])) expected.push_back(j);
        Require(actual == expected, "Grid query differs from exhaustive AABB scan");
    }
}

static void TestGrid()
{
    PhysicsBroadPhase grid;
    grid.Build({});
    Require(grid.CellCount() == 0 && grid.LargeCount() == 0, "Empty grid");
    std::vector<AABB> boxes = {
        {{0, 0, 0}, {1000000, 0.1f, 1000000}}, // large floor
        {{-2, 0.4f, -2}, {0.5f, 0.5f, 0.5f}},
        {{-2, 20, -2}, {0.5f, 0.5f, 0.5f}}, // above floor, not touching
        {{0, 2, 0}, {2, 2, 2}}, // touches cell boundaries
        {{2, 2, 0}, {0, 0, 0}},
        {{1e20f, 0, 0}, {1, 1, 1}} // no integer conversion overflow
    };
    grid.Build(boxes);
    Require(grid.LargeCount() == 2, "Large/out-of-range bounds must bypass grid");
    Require(grid.CellCount() <= (boxes.size() - 2) * grid.MaxCellsPerCollider, "Large floor expanded grid");
    CheckQueries(grid, boxes);
    boxes[0] = {{-2, 0.4f, -2}, {0.5f, 0.5f, 0.5f}};
    grid.Update(0, boxes[0]);
    Require(grid.LargeCount() == 1, "Large-to-small resize");
    CheckQueries(grid, boxes);
    boxes[1] = {{0, 0, 0}, {1000, 1000, 1000}};
    grid.Update(1, boxes[1]);
    Require(grid.LargeCount() == 2, "Small-to-large resize");
    CheckQueries(grid, boxes);

    std::mt19937 random(42017);
    std::uniform_real_distribution<float> position(-40, 40), extent(0.05f, 4);
    for (int scene = 0; scene < 60; ++scene)
    {
        boxes.clear();
        for (int i = 0; i < 120; ++i)
            boxes.push_back({{position(random), position(random), position(random)},
                            {extent(random), extent(random), extent(random)}});
        grid.Build(boxes);
        CheckQueries(grid, boxes);
        for (int mutation = 0; mutation < 30; ++mutation)
        {
            const size_t index = random() % boxes.size();
            boxes[index].center = {position(random), position(random), position(random)};
            grid.Update(index, boxes[index]);
            CheckQueries(grid, boxes);
        }
    }
}

static entt::entity Body(entt::registry& reg, glm::vec3 position, bool dynamic, glm::vec3 half = glm::vec3(0.5f))
{
    const auto entity = reg.create();
    reg.emplace<TransformComponent>(entity, position, glm::vec3(0), glm::vec3(1));
    auto& body = reg.emplace<RigidbodyComponent>(entity);
    body.type = dynamic ? RigidbodyType::Dynamic : RigidbodyType::Static;
    body.useGravity = false;
    auto& collider = reg.emplace<ColliderComponent>(entity);
    collider.halfExtents = half;
    return entity;
}

static void TestSolver()
{
    PhysicsSystem system;
    GameTimer timer;
    FrameContext context{timer, {}, 1.0f / 60};
    entt::registry reg;
    system.Update(reg, context);
    Require(PhysicsStats::BroadPhase().possiblePairs == 0, "Empty physics step");

    // EnTT visits these in reverse insertion order: A, B, C.
    Body(reg, {3.3f, 0, 0}, false);
    Body(reg, {1.7f, 0, 0}, false);
    const auto a = Body(reg, {1.9f, 0, 0}, true);
    system.Update(reg, context);
    Require(PhysicsStats::GetFrameCollisionCount() == 2, "Correction-created collision was missed");
    Require(std::abs(reg.get<TransformComponent>(a).position.x - 2.3f) < 1e-5f, "Sequential pair order changed");

    reg.clear();
    const auto floor = Body(reg, {0, 0, 0}, false, {1000000, 0.1f, 1000000});
    const auto cube = Body(reg, {0, 3, 0}, true);
    reg.get<RigidbodyComponent>(cube).useGravity = true;
    for (int step = 0; step < 180; ++step) system.Update(reg, context);
    Require(std::abs(reg.get<TransformComponent>(cube).position.y - 0.6f) < 1e-4f, "Cube fell through large floor");
    Require(PhysicsStats::BroadPhase().largeColliders == 1, "Floor was not automatically separated");

    reg.get<ColliderComponent>(floor).isTrigger = true;
    reg.get<TransformComponent>(cube).position.y = 0.55f;
    reg.get<RigidbodyComponent>(cube).useGravity = false;
    reg.get<RigidbodyComponent>(cube).velocity = glm::vec3(0);
    system.Update(reg, context);
    Require(PhysicsStats::GetFrameCollisionCount() == 1, "Trigger contact disappeared");
    Require(reg.get<TransformComponent>(cube).position.y == 0.55f, "Trigger pushed cube");
}

static glm::vec3 Vec(const nlohmann::json& value)
{
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

static void TestStressScene(const char* path)
{
    std::ifstream stream(path);
    nlohmann::json scene;
    stream >> scene;
    entt::registry reg;
    std::vector<AABB> boxes;
    for (const auto& item : scene.at("entities"))
    {
        if (!item.contains("collider")) continue;
        const auto entity = reg.create();
        const auto& t = item.at("transform");
        auto& transform = reg.emplace<TransformComponent>(entity, Vec(t.at("position")), Vec(t.at("rotation")), Vec(t.at("scale")));
        auto& collider = reg.emplace<ColliderComponent>(entity);
        const auto& c = item.at("collider");
        collider.type = c.value("type", "aabb") == "sphere" ? ColliderType::Sphere : ColliderType::AABB;
        collider.radius = c.value("radius", 0.5f);
        collider.halfExtents = Vec(c.at("halfExtents"));
        collider.offset = Vec(c.at("offset"));
        collider.friction = c.value("friction", 0.5f);
        collider.restitution = c.value("restitution", 0.0f);
        collider.isTrigger = c.value("isTrigger", false);
        if (item.contains("rigidbody"))
        {
            const auto& r = item.at("rigidbody");
            auto& body = reg.emplace<RigidbodyComponent>(entity);
            body.type = r.at("type") == "dynamic" ? RigidbodyType::Dynamic : RigidbodyType::Static;
            body.mass = r.value("mass", 1.0f);
            body.velocity = Vec(r.at("velocity"));
            body.acceleration = Vec(r.at("acceleration"));
            body.useGravity = r.value("useGravity", true);
        }
        boxes.push_back(MakeScaledAABB(transform.position, transform.scale, collider));
    }
    if (scene.value("name", "") == "PhysicsMixed512")
    {
        size_t cubes = 0, spheres = 0;
        for (const auto& item : scene.at("entities"))
        {
            if (!item.contains("rigidbody") || item.at("rigidbody").at("type") != "dynamic") continue;
            if (item.at("mesh").at("primitive") == "cube")
            {
                ++cubes;
                Require(item.at("collider").at("type") == "aabb", "Cube collider mismatch");
            }
            else if (item.at("mesh").at("primitive") == "sphere")
            {
                ++spheres;
                Require(item.at("collider").at("type") == "sphere", "Sphere collider mismatch");
            }
        }
        Require(cubes == 256 && spheres == 256, "Mixed scene must contain exactly 256 cubes and 256 spheres");
    }
    PhysicsBroadPhase grid;
    grid.Build(boxes);
    CheckQueries(grid, boxes);
    const auto possible = boxes.size() * (boxes.size() - 1) / 2;
    std::cout << "Stress scene initial snapshot: " << grid.AabbTests() << " AABB checks / "
              << possible << " exhaustive pairs; " << grid.LargeCount() << " large collider(s)\n";
    Require(grid.LargeCount() == 1, "Stress scene floor should bypass grid");
    Require(grid.AabbTests() < possible / 2, "Stress scene candidate reduction is insufficient");

    PhysicsSystem system;
    GameTimer timer;
    FrameContext context{timer, {}, 1.0f / 60};
    for (int step = 0; step < 600; ++step)
    {
        system.Update(reg, context);
        for (auto entity : reg.view<TransformComponent, RigidbodyComponent>())
        {
            const auto& t = reg.get<TransformComponent>(entity);
            Require(std::isfinite(t.position.x) && std::isfinite(t.position.y) && std::isfinite(t.position.z), "Nonfinite position");
            if (reg.get<RigidbodyComponent>(entity).IsDynamic())
                Require(t.position.y > -1.0f, "Stress scene body fell through floor");
        }
    }
    std::cout << "Stress scene: 600 physics steps passed\n";
}

int main(int argc, char** argv)
{
    try
    {
        TestGrid();
        TestSolver();
        TestNarrowPhase();
        for (int i = 1; i < argc; ++i) TestStressScene(argv[i]);
        std::cout << "PASS: grid oracle, movement/resize, large floors, correction-created contacts, triggers, sphere/sphere and sphere/box narrow phase\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

