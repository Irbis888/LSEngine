# Resource manager overview

Этот файл описывает, как в Try2 устроен `ResourceManager`: какие CPU-ресурсы он хранит, как создаются mesh/material/texture, как это попадает в renderer и как сцена может создавать ресурсы из JSON.

## Где лежит код

- `ResourceManager.h/.cpp` - CPU-side база ресурсов: meshes, materials, textures.
- `D3DRenderAdapter.cpp` - ограниченная за кадр очередь GPU-загрузки и привязка готовых ресурсов.
- `JobSystem.h/.cpp` - независимая CPU-обёртка над enkiTS; никаких ресурсов или GPU внутри.
- `Engine.cpp` - запуск worker threads и остановка загрузки перед уничтожением движка.
- `SceneFactory.h/.cpp` - удобная фабрика entity/primitive/camera.
- `SceneSerializer.cpp` - загрузка ресурсов из JSON-сцены.
- `DemoScene.cpp` и `Scenes/DemoScene.json` - примеры использования.

## Фоновая загрузка в движке

`ResourceManager` сразу в конструкторе создаёт три `ImageData` без обращения к диску:
шахматный albedo-плейсхолдер 2×2, белую текстуру для solid material и плоскую normal map.
`Engine::Init` затем создаёт до четырёх enkiTS workers (оставляя один аппаратный поток главному)
и вызывает `ResourceManager::InitLoading(mJobs)`.

`Engine::LoadScene` теперь принимает запрос и возвращает сразу. `true` означает, что запрос принят;
окончание и ошибка доступны через `GetSceneLoadProgress()`. Worker вызывает `SceneSerializer::Read`:
читает JSON, проверяет значения и создаёт обычные C++-описания объектов. Большой JSON уничтожается на worker,
а не в рендере. Пока чтение не завершилось, старая сцена продолжает рисоваться; ошибка чтения её сохраняет.

`Engine::Update` публикует описания до исчерпания временного бюджета, без лимита по числу объектов.
`SceneLoadingBudget`: 8 ms при первоначальном открытии и 4 ms при последующих подгрузках;
`SetSceneLoadingBudget` позволяет изменить оба значения. Удаление старого registry делит этот бюджет с созданием.
Камера и свет идут первыми, даже если записаны в конце JSON. Старый registry меняется через swap,
его компоненты удаляются также небольшими порциями. ECS, материалы и запросы ресурсов остаются на главном потоке.
Во время публикации физика приостановлена и сохранение неполной сцены запрещено.
Редактор показывает состояние чтения, число созданных объектов и готовых текстур; завершение CPU-публикации
не означает завершения texture uploads. Счётчик текстур относится ко всему кешу ResourceManager.
`SceneSerializer::Load` для инструментов/снимков остаётся синхронным и использует те же Read/CreateEntity.

В этом режиме `LoadTexture` / `LoadMesh` только регистрируют запрос и сразу возвращают стабильный ID.
Состояния ресурсов: `Loading` → `CpuReady` → `Ready`, либо `Failed` с текстом ошибки.
В начале каждого `D3DRenderAdapter::BeginFrame`, после reset command list:

1. Три встроенные текстуры один раз загружаются на GPU до обычной очереди.
2. `PumpLoading` забирает готовые результаты из защищённой mutex очереди в пределах временного бюджета.
3. Несколько consumer-задач непрерывно разбирают очередь запросов. После одного файла consumer сразу берёт
   следующий, без ожидания кадра. Текстура декодируется на worker; каждая модель использует свой `Assimp::Importer`.
   Workers не трогают resource maps, ECS, материалы и D3D12.
4. Главный поток публикует `ImageData` / mesh, создаёт материалы импортированной модели и ставит её текстуры в очередь.
5. GPU pump записывает копирования и transitions. Рендер идёт дальше; `Wait` для незавершённых задач в кадре не вызывается.

Число одновременно запущенных consumers ограничено workers и `maxConcurrentLoads` (по умолчанию 4, максимум 8).
Тысяча запросов хранится в очереди ResourceManager, а не превращается в тысячу задач enkiTS;
в этой интеграции небольшое число consumer-задач не переполняет pipe и не запускает декодер на главном потоке.
Consumers запускаются из `LoadTexture` / `LoadMesh`, а не только из кадрового pump.

Backpressure зависит от байтов готовых данных: 64 MiB по умолчанию, настройка `SetLoadingMemoryBudget`.
Учитываются pixels/subresources, вершины/индексы/submeshes моделей и описания материалов,
включая результаты workers, ещё не опубликованные главным потоком. Публикация не снимает charge с mesh/image;
это делают `FinishMeshUpload` / `FinishTextureUpload` после записи GPU upload. Освобождённые описания материалов
снимаются с charge при публикации модели. При заполнении памяти consumer уступает scheduler другим задачам;
после освобождения байтов главный поток возобновляет consumers сразу, не откладывая до следующего кадра.
`GetLoadingStats` показывает текущие/пиковые байты, число готовых результатов и активных декодеров.

Предел мягкий: декодируемые сейчас файлы могут добавить до одного готового ресурса на worker сверх бюджета;
размер неизвестен до декодирования, его внутренние временные allocations отдельно не ограничиваются.
Постоянные CPU-копии уже загруженных ресурсов сохраняются и в бюджет ожидающих данных не входят.

`D3DRenderAdapter::UploadBudget` задаёт 64 MiB полезных данных и 12 ms на публикацию/финализацию очереди,
без лимита числа текстур или мешей. CPU-публикация использует до половины этого временного бюджета;
её время учитывается в общем времени pump. `SetUploadBudget` позволяет изменить настройки.
Примитивы загружаются при первом draw вне этого бюджета.
Время и объём — мягкие пределы: хотя бы один ресурс обрабатывается за кадр, даже если сам превышает лимит.
Разбиения одной большой текстуры/модели на несколько кадров пока нет.

`GetOrLoadMaterial` только выбирает SRV: пока albedo не готов, берёт шахматную текстуру;
для незагруженной normal map — плоскую normal map; для отсутствующего albedo — белую.
После загрузки cached material автоматически начинает использовать новый SRV. Общий descriptor плейсхолдера
не перезаписывается, поэтому ранее отправленные кадры остаются корректными.
`Ready` означает, что копирование и barrier уже записаны перед draw в той же direct queue.
Завершение этих команд на GPU проверяется отдельно по fence; только после него освобождаются upload heaps.

Для модели в `Loading` пока пропускается draw. После импорта её первый запрос на draw ставит mesh
в GPU-очередь; buffers создаются в следующем `BeginFrame`. Примитивы создаются на CPU сразу, а их buffers
создаются лениво при первом draw, перед ним в том же command list. Они не ждут GPU-очередь моделей;
последующие draw используют cached buffers. Plane и cube имеют по одной общей immutable CPU geometry,
сферы кэшируются по нормализованной паре `(slices, stacks)`. Все MeshID одинакового примитива используют
одни vertex/index GPU buffers, но имеют независимые submesh/material metadata. CPU-геометрия доступна
через `Mesh::GetVertices()` / `GetIndices()`: собственные `vertices` / `indices` используются для импортированных
и вручную созданных meshes; у примитивов данные находятся в `sharedGeometry`.
Пока текстуры грузятся, примитив рисуется с плейсхолдером.
Назначенные до окончания импорта mesh/submesh material overrides сохраняются.

`Engine::Shutdown` запрещает новые запросы, отменяет ещё не отправленные, дожидается только уже отправленной CPU-работы
и останавливает scheduler. Приложение затем дожидается GPU перед освобождением ImGui; renderer также делает flush
при уничтожении. Scheduler должен жить дольше ResourceManager либо loading нужно остановить явно.

Для инструментов и существующих CPU-тестов `ResourceManager` без `InitLoading` оставляет синхронную загрузку.
Ошибки такого чтения выбрасываются сразу; в движке ошибка сохраняется в ресурсе и выводится в журнал,
а плейсхолдер остаётся доступным. Асинхронный неудачный запрос остаётся в cache; повторный `LoadTexture` возвращает тот же ID.

Tracy показывает зоны `Texture CPU read and decode`, `Model CPU read and decode`,
`Publish CPU resources`, `Bounded GPU resource finalization` и счётчики
активных/ожидающих CPU jobs, байтов ожидающих GPU данных, uploads/bytes за кадр.

Проверка: `tests/RunTextureTests.ps1` включает скрытое D3D12-окно, кадры с занятыми workers,
замену плейсхолдера, лимит GPU uploads, асинхронные ошибки, импорт модели и material overrides,
GPU readback и проверку D3D12 debug diagnostics. `SceneSwitchStreamingTests.cpp` дополнительно проверяет
настоящий Engine со сценой `TextureStreaming1000.json`: рендер во время чтения, публикацию ECS по времени,
плейсхолдер в draw, постепенную готовность всех 1000 DDS, сохранение предыдущей сцены при ошибке JSON и shutdown.
Consumer-тесты проверяют обработку очереди без кадров, byte backpressure для textures/models,
возобновление после GPU uploads и общую CPU/GPU геометрию с независимыми материалами.
`tests/MeasureResourceLoading.ps1` сравнивает сохранённую сборку до правок с текущей в одинаковом Debug harness,
прогревая одну сцену/1000 DDS перед каждым процессом. Условия и результаты описаны в `RESOURCE_LOADING_BENCHMARK.md`.

## Главная идея

`ResourceManager` хранит CPU-описания ресурсов и выдаёт маленькие числовые ID:

```cpp
using MeshID = uint32_t;
using MaterialID = uint32_t;
using TextureID = uint32_t;
```

Entity не хранит сами вершины, материалы или текстуры. Entity обычно хранит только:

```cpp
struct MeshComponent
{
    MeshID meshID;
};
```

А renderer по этому `MeshID` спрашивает у `ResourceManager`, какие вершины, индексы и материалы надо нарисовать.

## Что хранит ResourceManager

Внутри сейчас есть три основных контейнера:

```cpp
std::unordered_map<MeshID, Mesh> mMeshes;
std::unordered_map<MaterialID, Material> mMaterials;
std::unordered_map<TextureID, Texture> mTextures;
std::unordered_map<std::wstring, TextureID> mTextureIDsByFilename;
```

То есть ResourceManager хранит:

- `Mesh` - вершины, индексы, submesh-информацию.
- `Material` - цвет, roughness, ссылки на albedo/normal textures.
- `Texture` - имя, filename и `ImageData` с данными изображения в CPU-памяти.
- `mTextureIDsByFilename` - cache, чтобы одна и та же texture filename не создавала новый `TextureID` каждый раз.

Важно: это CPU-side storage. D3D12 buffers, SRV descriptors и GPU textures хранятся не здесь, а в `D3DRenderAdapter`.

## ID-генерация

ID сейчас генерируются простыми static-счётчиками в `ResourceManager.cpp`:

```cpp
static MeshID gNextMeshID = 1;
static MaterialID gNextMaterialID = 1;
static TextureID gNextTextureID = 1;
```

Новый ресурс получает следующий ID, после чего кладётся в соответствующий `unordered_map`.

Нулевой ID фактически используется как "ресурса нет". Например, `Material::albedo = 0` означает, что albedo texture не назначена.

## Vertex

Один vertex сейчас содержит:

```cpp
glm::vec3 Position;
glm::vec3 Normal;
glm::vec3 TangentU;
glm::vec2 TexC;
```

Это соответствует input layout в renderer:

- `POSITION`
- `NORMAL`
- `TANGENT`
- `TEXCOORD`

Поэтому любой mesh, созданный руками или загруженный через Assimp, должен заполнить эти поля.

## Mesh

`Mesh` хранит:

```cpp
std::vector<Vertex> vertices;
std::vector<uint32_t> indices;
std::vector<Submesh> submeshes;
uint32_t materialVersion = 1;
```

`Submesh` хранит:

```cpp
uint32_t indexOffset;
uint32_t indexCount;
MaterialID material;
```

Один `Mesh` может содержать несколько submesh-частей. Это нужно для загруженных моделей, где разные части используют разные материалы. Например, sponza загружается как один большой `Mesh`, но внутри у него много submesh.

`materialVersion` нужен, чтобы renderer мог понять, что у CPU mesh поменялись материалы submesh-ов, и синхронизировать GPU-side metadata.

## Material

`Material` сейчас содержит:

```cpp
std::string name;
TextureID albedo = 0;
TextureID normal = 0;
glm::vec3 color = glm::vec3(1.0f);
float roughness = 0.5f;
```

Материал может быть:

- solid - без текстур, только `color` и `roughness`;
- textured - с `albedo` и, опционально, `normal`.

Даже textured material умножается на `color` в shader path. Поэтому `color = [1, 1, 1]` означает "показать текстуру как есть", а другой цвет будет tint-ом.

## Texture

`Texture` хранит CPU-метаданные и данные изображения:

```cpp
std::string name;
std::wstring filename;
ImageData imageData;
```

`ResourceManager::LoadTexture` регистрирует запрос; worker получает `ImageData` без GPU-ресурсов. Без `InitLoading` чтение выполняется сразу. Для `.dds` (без учёта регистра) используется CPU-часть `DDSTextureLoader`; остальные форматы декодируются через `stb_image` в RGBA8. `ImageData` владеет пикселями или сжатыми DDS-блоками, хранит формат, размеры, mip-уровни, массивы и offsets/rowPitch/slicePitch каждого subresource. Offsets остаются корректными при копировании и перемещении данных. Загрузка в `ID3D12Resource` происходит позже, в `D3DRenderAdapter::UploadTexture`.

## Создание mesh

### CreateMesh

```cpp
MeshID ResourceManager::CreateMesh(Mesh mesh)
```

Просто принимает готовый CPU mesh, выдаёт ему новый `MeshID` и сохраняет в `mMeshes`.

### LoadMesh

```cpp
MeshID ResourceManager::LoadMesh(const std::string& path)
```

Регистрирует модель для импорта через Assimp в worker; без `InitLoading` импортирует сразу.

Используемые Assimp flags:

- `aiProcess_Triangulate` - всё превращается в треугольники.
- `aiProcess_ConvertToLeftHanded` - конвертация под left-handed coordinate system.
- `aiProcess_FlipUVs` - переворот UV.
- `aiProcess_GenNormals` - генерация normals, если их нет.
- `aiProcess_CalcTangentSpace` - tangents для normal mapping.

Алгоритм:

1. Worker читает файл через Assimp и копирует вершины, индексы, submeshes и описания материалов в собственный результат.
2. Относительные texture paths проверяются рядом с моделью; изображения пока не читаются.
3. `PumpLoading` на главном потоке создаёт материалы и вызывает `LoadTexture` для их изображений.
4. Локальные material indices заменяются на `MaterialID`, применяются сохранённые overrides.
5. Готовый mesh публикуется под уже выданным `MeshID`.

Normal texture ищется в нескольких Assimp texture slots:

- `aiTextureType_NORMALS`
- `aiTextureType_HEIGHT`
- `aiTextureType_DISPLACEMENT`

Это сделано потому, что разные модели экспортируют normal map в разные semantic-поля.

## Генерация примитивов

Есть три primitive mesh generator:

```cpp
MeshID CreatePlane(MaterialID material);
MeshID CreateCube(MaterialID material);
MeshID CreateSphere(MaterialID material, uint32_t slices = 32, uint32_t stacks = 16);
```

Все примитивы создаются в локальном размере примерно `[-0.5, 0.5]`, а реальный размер задаётся через `TransformComponent::scale`.

### Plane

Plane лежит в XZ-плоскости, normal смотрит вверх:

```cpp
normal = (0, 1, 0)
```

Используется для пола/платформы.

### Cube

Cube создаётся из отдельных граней, чтобы у каждой грани были правильные normal/tangent/uv. Индексы сейчас идут в порядке, рассчитанном под наружные стороны.

### Sphere

Sphere строится по `slices` и `stacks`, с UV по широте/долготе. Минимальные значения:

```cpp
slices >= 3
stacks >= 2
```

## Создание материалов

### CreateMaterial

```cpp
MaterialID CreateMaterial(const Material& mat)
```

Низкоуровневый метод: принимает готовый `Material` и кладёт его в `mMaterials`.

### CreateSolidMaterial

```cpp
MaterialID CreateSolidMaterial(
    const std::string& name,
    const glm::vec3& color,
    float roughness = 0.5f);
```

Создаёт материал без albedo/normal textures. Renderer потом подставит fallback textures:

- diffuse fallback: встроенная белая RGBA-текстура
- normal fallback: встроенная плоская normal map

Цвет и roughness всё равно попадут в material constant buffer.

### CreateTexturedMaterial

Есть две формы:

```cpp
MaterialID CreateTexturedMaterial(const MaterialDesc& desc);
```

и удобная overload:

```cpp
MaterialID CreateTexturedMaterial(
    const std::string& name,
    const std::wstring& albedoTexture,
    const std::wstring& normalTexture = L"",
    const glm::vec3& color = glm::vec3(1.0f),
    float roughness = 0.5f);
```

Она создаёт `Material`, а запросы изображений передаёт в `LoadTexture`.

Пример:

```cpp
MaterialID cubeMaterial = resources.CreateTexturedMaterial(
    "DemoCube",
    L"bricks2.dds",
    L"bricks2_nmap.dds",
    glm::vec3(1.0f),
    0.45f);
```

## LoadTexture

```cpp
TextureID ResourceManager::LoadTexture(const std::wstring& filename)
```

Метод резолвит путь, возвращает `TextureID` и ставит запрос в CPU-очередь. Изображение появляется в `Texture::imageData` при публикации результата. Без `InitLoading` оно читается сразу. GPU-ресурсы здесь не создаются.

Кеш использует абсолютный нормализованный путь, поэтому повторная загрузка возвращает прежний ID без повторного чтения. Путь ищется напрямую, затем в `../../Textures`, как раньше в renderer. Исходный filename сохраняется в `Texture`.

Для DDS `DirectX::LoadDDSImageFromFile` проверяет заголовок и объём данных, сохраняя сжатие и mip-цепочку. Остальные изображения читает `stb_image`; результат — RGBA8 с одним mip-уровнем. Unicode filenames поддерживаются через открытие файла по wide-пути.

В синхронном режиме ошибка чтения/декодирования выбрасывает `std::runtime_error` и не попадает в cache. В асинхронном режиме устанавливаются `Failed` и `Texture::error`; renderer сохраняет плейсхолдер.

## Смена материалов у mesh

Есть два метода:

```cpp
void SetMeshMaterial(MeshID mesh, MaterialID material);
void SetSubmeshMaterial(MeshID mesh, uint32_t submeshIndex, MaterialID material);
```

`SetMeshMaterial` меняет материал у всех submesh-ов.

`SetSubmeshMaterial` меняет материал только у одного submesh.

После изменения увеличивается:

```cpp
++mesh.materialVersion;
```

Renderer использует это, чтобы обновить GPU-side копию submesh material IDs без полной перезагрузки vertex/index buffers.

## Как ResourceManager связан с renderer

`ResourceManager` не знает про D3D12. Связь идёт через:

```cpp
mRenderAdapter->SetResourceManager(&mResourceManager);
```

После этого `D3DRenderAdapter` может читать CPU-ресурсы.

## Очередь GPU uploads

`GetMeshGPU` возвращает cached buffers. Для CPU-ready примитива он сразу вызывает `UploadMesh`,
поэтому `DrawMesh` / `DrawSubmesh` могут нарисовать его в том же кадре. Остальные готовые CPU mesh
регистрируются в очереди с возвратом `nullptr`; их draw пропускается до финализации в `BeginFrame`.
`UploadMesh` создаёт vertex/index default buffers, upload buffers, views и submesh metadata.
Изменение материалов готового mesh синхронизирует metadata без пересоздания geometry.

`GetOrLoadMaterial` кэширует параметры и каждый draw обновляет индексы SRV по наличию готовых GPU-текстур.
Файлы во время draw не читаются, GPU texture uploads во время draw не выполняются.

`UploadTexture(TextureID)` получает `ImageData`, создаёт default texture и upload heap,
копирует все subresources через `UpdateSubresources`, записывает barrier, создаёт отдельный SRV и сохраняет `TextureGPU`.
Прямые вызовы `UploadMesh` / `UploadTexture` доступны для инструментов и требуют открытого command list;
обычный рендер пользуется ограниченной очередью для моделей и текстур, а примитивы загружает при первом draw.
Upload buffers текстур и мешей удерживаются до `uploadCompleteFence`, затем удаляются.

## JSON-сцены и ресурсы

`SceneSerializer.cpp` умеет создавать mesh и material из JSON.

Пример primitive mesh с solid material:

```json
"mesh": {
    "source": "primitive",
    "primitive": "cube",
    "material": {
        "name": "RedCubeMaterial",
        "color": [1.0, 0.1, 0.1],
        "roughness": 0.5
    }
}
```

Пример primitive mesh с textured material:

```json
"mesh": {
    "source": "primitive",
    "primitive": "cube",
    "material": {
        "name": "BrickCubeMaterial",
        "color": [1.0, 1.0, 1.0],
        "roughness": 0.45,
        "albedo": "bricks2.dds",
        "normal": "bricks2_nmap.dds"
    }
}
```

Пример model mesh:

```json
"mesh": {
    "source": "model",
    "path": "../../Common/sponza.obj"
}
```

Старый формат с прямым mesh ID пока тоже поддерживается:

```json
"mesh": {
    "id": 3
}
```

Но он хрупкий, потому что зависит от порядка создания ресурсов. Лучше использовать `source: primitive` или `source: model`.

## SceneFactory и ResourceManager

`SceneFactory` не хранит ресурсы сам. Он получает ссылку на `ResourceManager`:

```cpp
SceneFactory factory(world.registry, resources);
```

И использует его, когда нужно создать primitive mesh:

```cpp
MeshID mesh = CreatePrimitiveMesh(type, material);
```

После этого создаётся entity с:

```cpp
TagComponent
TransformComponent
MeshComponent
```

То есть `SceneFactory` связывает ECS и ResourceManager, но не занимается GPU.

## Отладочные методы

Есть три print-метода:

```cpp
PrintAllMeshes();
PrintAllMaterials();
PrintAllTextures();
```

Они печатают в console:

- список mesh IDs;
- количество vertices/indices/submeshes;
- material IDs у submesh;
- список materials;
- texture IDs и filenames.

Это полезно, когда надо понять, что реально создалось после загрузки модели или JSON-сцены.

## Типичный путь ресурса

1. `Engine::LoadScene` отправляет чтение и разбор JSON в job; `Engine::Update` постепенно создаёт entity, material и CPU primitive.
2. `LoadTexture` возвращает ID сразу, данные пока имеют состояние `Loading`.
3. `BeginFrame` отправляет CPU-запрос worker; отрисовка использует плейсхолдер.
4. Следующий `PumpLoading` публикует готовое `ImageData`.
5. GPU pump загружает изображение в пределах бюджета.
6. Следующий draw материала выбирает его новый SRV автоматически.
7. После fence освобождается временный upload heap.

## Текущие ограничения

- Нет unload/release API для CPU ресурсов.
- ID-генераторы static, а не поля `ResourceManager`; несколько ResourceManager в одном процессе будут делить счётчики.
- Нет cache для `LoadMesh(path)`: одна и та же модель, загруженная дважды, создаст два разных `MeshID`.
- Material параметры и texture IDs перечитываются при draw; явного versioning/change notification пока нет.
- Texture cache есть на CPU-side по нормализованному абсолютному пути и на GPU-side по `TextureID`.
- CPU-данные остаются в памяти после GPU upload; unload/cancellation при смене сцены пока не реализованы.
- JSON читается и разбирается в job; создание ECS и генерация отдельных примитивов идут на главном потоке порциями. Один очень большой примитив может превысить мягкий бюджет.
- Direct queue используется и для uploads, и для draw; отдельной copy queue нет.
- Для non-DDS используется набор форматов `stb_image`; mip-уровни автоматически не генерируются.
- `GetMesh/GetMaterial/GetTexture` используют `assert`, поэтому в Release неправильный ID может приводить к плохим последствиям без красивого exception.
- Нет asset database, GUID, hot reload текстур/материалов и dependency tracking.

## Что логично добавить дальше

1. Сделать ID-генераторы полями `ResourceManager`, а не static-переменными.
2. Добавить cache для `LoadMesh(path)`.
3. Добавить material versioning, чтобы менять color/roughness/texture во время работы.
4. Добавить явный asset descriptor формат: material assets, mesh assets, texture assets.
5. Добавить проверку существования texture/model files на этапе загрузки сцены.
6. Добавить генерацию mip-уровней для non-DDS изображений.
7. Добавить unload/reference counting для ресурсов.
8. Добавить editor UI для просмотра meshes/materials/textures и переназначения материалов.
