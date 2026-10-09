# Physics system overview

Этот файл описывает текущую физику в движке Try2: какие компоненты используются, как проходит шаг симуляции, как работают столкновения и какие ограничения есть у реализации.

## Где лежит код

- `PhysicsCommons.h` - общие типы физики: `RigidbodyComponent`, `ColliderComponent`, `AABB`, `CollisionManifold`, helper-функции и `PhysicsStats`.
- `PhysicsSystem.h/.cpp` - ECS-система, которая каждый physics tick интегрирует движение и разрешает столкновения.
- `Application.cpp` - fixed timestep loop, который вызывает `Engine::PhysicsUpdate`.
- `Engine.cpp` - регистрирует `PhysicsSystem` в списке physics systems.
- `DemoScene.cpp` и `Scenes/DemoScene.json` - примеры объектов с физикой.

## Главная идея

Физика работает через ECS-компоненты. Entity начинает участвовать в физике, если у неё есть:

- `TransformComponent` - позиция, поворот и scale объекта.
- `RigidbodyComponent` - физическое тело: скорость, масса, гравитация, тип тела.
- `ColliderComponent` - форма столкновения и параметры контакта.

Для самого факта столкновения достаточно `TransformComponent + ColliderComponent`. Для движения и реакции на импульсы нужен ещё `RigidbodyComponent`.

## RigidbodyComponent

```cpp
struct RigidbodyComponent
{
    RigidbodyType type = RigidbodyType::Dynamic;
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 acceleration = glm::vec3(0.0f);
    float mass = 1.0f;
    bool useGravity = true;
};
```

Типы тел:

- `Static` - неподвижное тело. Не интегрируется, имеет inverse mass = 0. Обычно используется для пола, стен, платформ.
- `Dynamic` - обычное физическое тело. Двигается под действием velocity, acceleration и gravity.
- `Kinematic` - сейчас фактически ведёт себя как тело с inverse mass = 0 и не интегрируется в `Integrate`, то есть это зарезервированный тип под будущую ручную анимацию/движение.

Масса используется через `InverseMass()`. Если тело не `Dynamic`, inverse mass равен `0`. Для dynamic-тела масса ограничена снизу через `Physics::MinMass`, чтобы не делить на ноль.

## ColliderComponent

```cpp
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
```

Поля:

- `type` - `AABB` или `Sphere`.
- `offset` - смещение коллайдера относительно `TransformComponent::position`.
- `halfExtents` - половинный размер AABB-коллайдера в локальных единицах.
- `radius` - радиус sphere-коллайдера.
- `isTrigger` - если true, столкновение детектится, но физически не разрешается.
- `restitution` - упругость столкновения. Чем больше, тем сильнее отскок.
- `friction` - простое затухание касательной скорости после столкновения.

Важно: текущая collision-система всё приводит к scaled AABB через `MakeScaledAABB`. Даже `ColliderType::Sphere` сейчас участвует в столкновениях как AABB с размером `radius * max(scale)`. Функция `IntersectsSphere` есть в `PhysicsCommons.h`, но в `PhysicsSystem` пока не используется.

## Fixed timestep

Физика вызывается не напрямую от render delta time, а через fixed timestep loop в `Application::Run`.

Схема:

1. Каждый кадр берётся `mTimer.DeltaTime()`.
2. Delta ограничивается сверху до `0.1f`, чтобы большой лаг не взорвал симуляцию.
3. Время накапливается в `accumulator`.
4. Пока `accumulator >= fixed_dt`, вызывается `PhysicsUpdate`.
5. За один render frame выполняется не больше `maxSteps = 5` physics steps.

В `FrameContext` передаётся:

```cpp
mFrameContext.physDT = fixed_dt;
```

А `PhysicsSystem::Update` выбирает:

```cpp
const float dt = context.physDT > 0.0f ? context.physDT : context.timer.DeltaTime();
```

То есть обычно физика идёт на фиксированном `physDT`, а обычный frame delta используется только как fallback.

## Шаг PhysicsSystem

`PhysicsSystem` получает ссылку на общий `Engine::mJobs`; отдельного scheduler нет.
`Update` выполняет синхронный шаг и возвращается только после завершения обеих CPU-задач:

```cpp
PhysicsStats::ResetFrameCollisionCount();
Prepare(reg);       // Переиспользуемые массивы указателей, resize массива AABB.
// IntegrateRange: Dispatch(High) + WaitHighPriority или прямой вызов.
// BoundsRange: Dispatch(High) + WaitHighPriority или прямой вызов.
mBroadPhase.Build(mBounds);
ResolveCollisions(); // Последовательный solver с обновлением сетки и повторным поиском.
```

Главный поток собирает указатели на компоненты до запуска задач. Рабочие задачи не обращаются
к registry и пишут только в свой диапазон тел или `bounds[i]`. Между Dispatch и Wait
нельзя изменять структуру registry; движок выполняет публикацию сцен и редактирование до/после шага.
Массивы очищаются с сохранением capacity, указатели собираются заново каждый шаг, поэтому
изменения registry между шагами допустимы.

`SetSchedulingSettings({parallel, parallelThreshold, rangeSize})` управляет режимом обработки.
Порог проверяется отдельно для интеграции и AABB. Последовательный и параллельный режимы
используют одинаковые `IntegrateRange` / `BoundsRange`; формулы внутри тела сохранены.
Начальные 128/64 проверены измерениями; итоговый порог по умолчанию — 8192 элементов,
минимальный диапазон — 64. Ниже порога отправка задач обходилась дороже сэкономленных вычислений.
Обе настройки доступны в Statistics и через `Engine::GetPhysics()`.

Физика имеет приоритет High; декодирование ресурсов и разбор сцены — Low. Ожидание
`WaitHighPriority` помогает только High-задачам и не подхватывает импорт/чтение файла.
ResourceManager допускает максимум `workers - 1` consumers (минимум один) и между файлами
вызывает `RunHighPriorityTasks`. Длительный уже начавшийся импорт не прерывается.
При единственном worker физическая работа может выполняться главным потоком.

## Интеграция движения

Подготовка массива интеграции проходит по всем entity с:

```cpp
TransformComponent + RigidbodyComponent
```

Для каждого dynamic-тела:

1. Если `useGravity == true`, к velocity добавляется `Physics::DefaultGravity * dt`.
2. К velocity добавляется `acceleration * dt`.
3. К позиции добавляется `velocity * dt`.

Формула:

```cpp
body.velocity += gravity * dt;
body.velocity += body.acceleration * dt;
transform.position += body.velocity * dt;
```

Гравитация сейчас:

```cpp
glm::vec3(0.0f, -9.81f, 0.0f)
```

## Детекция столкновений

Подготовка массива коллайдеров берёт все entity с:

```cpp
TransformComponent + ColliderComponent
```

Начальные AABB вычисляются независимо по индексам, затем `PhysicsBroadPhase` последовательно
строит пространственную сетку. Крупные коллайдеры, например пол, хранятся отдельно.
Кандидаты отсортированы по прежнему порядку collider-view; solver обрабатывает их последовательно.
После коррекции позиции границы и сетка обновляются, для сдвинутого первого тела выполняется
повторный поиск после текущего индекса пары. Так сохраняются контакты, появившиеся вследствие
предыдущих коррекций. Обновление границ в solver не распараллеливается.

Для каждой пары:

1. Коллайдеры превращаются в scaled AABB через `MakeScaledAABB`.
2. `GetColliderCollision` выбирает AABB/AABB, sphere/sphere или sphere/AABB narrow phase.
3. Если пересечение есть, создаётся `CollisionManifold`.

`CollisionManifold` содержит:

- `colliding` - есть ли столкновение.
- `normal` - направление, вдоль которого надо раздвигать объекты.
- `penetrationDepth` - глубина проникновения.

Нормаль выбирается по оси минимального overlap. То есть если объект меньше всего проник по Y, столкновение будет разрешаться вертикально.

## Разрешение столкновения

`ResolveCollision` сначала проверяет trigger:

```cpp
if (aCollider.isTrigger || bCollider.isTrigger)
    return;
```

Trigger-коллайдеры сейчас только считаются как collision event через `PhysicsStats::AddCollision`, но не двигают тела.

Затем вычисляются inverse masses:

```cpp
const float aInvMass = aBody ? aBody->InverseMass() : 0.0f;
const float bInvMass = bBody ? bBody->InverseMass() : 0.0f;
```

Если оба тела static/kinematic или вообще без rigidbody, сумма inverse mass равна нулю, и столкновение не разрешается.

### Позиционная коррекция

Объекты раздвигаются пропорционально inverse mass:

```cpp
const glm::vec3 correction = collision.normal * collision.penetrationDepth;
aTransform.position -= correction * (aInvMass / invMassSum);
bTransform.position += correction * (bInvMass / invMassSum);
```

Если один объект static, двигается только dynamic. Если оба dynamic, двигаются оба с учётом массы.

### Импульс

После раздвигания считается относительная скорость:

```cpp
const glm::vec3 relativeVelocity = bVelocity - aVelocity;
const float velocityAlongNormal = glm::dot(relativeVelocity, collision.normal);
```

Если тела уже расходятся, импульс не применяется:

```cpp
if (velocityAlongNormal > 0.0f)
    return;
```

Иначе считается импульс с учётом `restitution`:

```cpp
const float restitution = min(aCollider.restitution, bCollider.restitution);
const float impulseMagnitude = -(1.0f + restitution) * velocityAlongNormal / invMassSum;
```

Импульс меняет velocity dynamic-тел вдоль collision normal.

### Friction

После normal impulse применяется очень простая friction-модель:

1. Velocity раскладывается на normal component и tangent component.
2. Tangent component умножается на `1.0f - friction`.
3. Если касательная скорость стала почти нулевой, она зануляется.

Это не полноценная Coulomb friction, но для текущей демосцены достаточно, чтобы кубы/сферы не скользили бесконечно по полу.

## PhysicsStats

В `PhysicsCommons.h` есть `PhysicsStats`:

```cpp
PhysicsStats::ResetFrameCollisionCount();
PhysicsStats::AddCollision();
PhysicsStats::GetFrameCollisionCount();
```

Сейчас он хранит количество столкновений за physics frame. ImGui/statistics panel может использовать это для отладки.

`PhysicsStats::Timings()` и `PhysicsSystem::GetTimings()` возвращают время последнего
завершённого шага: prepare, integration (с Dispatch/Wait), initial AABB (с Dispatch/Wait),
Build сетки, solver (с обновлениями сетки и повторным поиском) и полный шаг в миллисекундах.
Также доступны числа тел/коллайдеров и фактический режим каждого CPU-этапа.
В Statistics видны все эти значения, переключатель Parallel physics, порог и размер диапазона;
пока физика выключена, отображаются показатели последнего завершённого шага.
Те же этапы размечены зонами и графиками Tracy.

Измерения последовательного/параллельного режимов: [PHYSICS_PARALLEL_BENCHMARK.md](PHYSICS_PARALLEL_BENCHMARK.md).

## Как добавить физический объект в C++

Минимальный dynamic cube:

```cpp
entt::entity cube = factory.CreatePrimitive(
    "FallingCube",
    PrimitiveType::Cube,
    material,
    glm::vec3(0.0f, 10.0f, 0.0f),
    glm::vec3(0.0f),
    glm::vec3(2.0f));

world.registry.emplace<RigidbodyComponent>(
    cube,
    RigidbodyComponent{
        RigidbodyType::Dynamic,
        glm::vec3(0.0f),
        glm::vec3(0.0f),
        1.0f,
        true });

world.registry.emplace<ColliderComponent>(
    cube,
    ColliderComponent{
        ColliderType::AABB,
        glm::vec3(0.0f),
        glm::vec3(0.5f),
        0.5f,
        false,
        0.05f,
        0.6f });
```

Минимальный static floor:

```cpp
world.registry.emplace<RigidbodyComponent>(
    floor,
    RigidbodyComponent{ RigidbodyType::Static });

world.registry.emplace<ColliderComponent>(
    floor,
    ColliderComponent{
        ColliderType::AABB,
        glm::vec3(0.0f),
        glm::vec3(0.5f, 0.1f, 0.5f),
        0.5f,
        false,
        0.0f,
        0.8f });
```

## Как добавить физический объект в JSON

В `Scenes/DemoScene.json` можно добавить entity с `rigidbody` и `collider`:

```json
{
    "tag": "JsonCube",
    "transform": {
        "position": [0.0, 10.0, 0.0],
        "rotation": [0.0, 0.0, 0.0],
        "scale": [2.0, 2.0, 2.0]
    },
    "mesh": {
        "source": "primitive",
        "primitive": "cube",
        "material": {
            "name": "JsonCubeMaterial",
            "color": [0.8, 0.2, 0.2],
            "roughness": 0.5
        }
    },
    "rigidbody": {
        "type": "dynamic",
        "velocity": [0.0, 0.0, 0.0],
        "acceleration": [0.0, 0.0, 0.0],
        "mass": 1.0,
        "useGravity": true
    },
    "collider": {
        "type": "aabb",
        "offset": [0.0, 0.0, 0.0],
        "halfExtents": [0.5, 0.5, 0.5],
        "radius": 0.5,
        "isTrigger": false,
        "restitution": 0.05,
        "friction": 0.6
    }
}
```

Для sphere-коллайдера:

```json
"collider": {
    "type": "sphere",
    "offset": [0.0, 0.0, 0.0],
    "halfExtents": [0.5, 0.5, 0.5],
    "radius": 0.5,
    "isTrigger": false,
    "restitution": 0.2,
    "friction": 0.4
}
```

Для sphere используются мировые центр и радиус из scaled AABB, а narrow phase учитывает форму сферы.

## Текущие ограничения

- Нет rotation-aware collider. AABB всегда осевой, поворот `TransformComponent::rotation` на коллайдер не влияет.
- Нет continuous collision detection. Быстрый объект может пролететь через тонкий объект при большом timestep.
- При плотных скоплениях число кандидатов пространственной сетки может приблизиться к O(n^2).
- Нет angular velocity, torque, inertia tensor и вращательной физики.
- Нет collision events/callbacks, только общий счётчик `PhysicsStats`.
- `Kinematic` тип пока зарезервирован и не имеет отдельной логики движения.
- Friction упрощённая и применяется как damping касательной velocity после collision impulse.

## Что логично добавить дальше

1. Настоящие collision events: `OnCollisionEnter`, `OnCollisionStay`, `OnCollisionExit`.
3. Debug draw коллайдеров.
4. Layer mask и collision filtering.
5. Kinematic bodies с ручным движением и push dynamic bodies.
7. Angular physics: rotation, angular velocity, inertia, torque.
