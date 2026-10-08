# Resource manager overview

Этот файл описывает, как в Try2 устроен `ResourceManager`: какие CPU-ресурсы он хранит, как создаются mesh/material/texture, как это попадает в renderer и как сцена может создавать ресурсы из JSON.

## Где лежит код

- `ResourceManager.h/.cpp` - CPU-side база ресурсов: meshes, materials, textures.
- `D3DRenderAdapter.cpp` - lazy upload CPU-ресурсов в GPU-ресурсы.
- `SceneFactory.h/.cpp` - удобная фабрика entity/primitive/camera.
- `SceneSerializer.cpp` - загрузка ресурсов из JSON-сцены.
- `DemoScene.cpp` и `Scenes/DemoScene.json` - примеры использования.

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

`ResourceManager::LoadTexture` читает файл и получает `ImageData` без GPU-ресурсов. Для `.dds` (без учёта регистра) используется CPU-часть `DDSTextureLoader`; остальные форматы декодируются через `stb_image` в RGBA8. `ImageData` владеет пикселями или сжатыми DDS-блоками, хранит формат, размеры, mip-уровни, массивы и offsets/rowPitch/slicePitch каждого subresource. Offsets остаются корректными при копировании и перемещении данных. Загрузка в `ID3D12Resource` происходит позже, в `D3DRenderAdapter::UploadTexture`.

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

Загружает модель через Assimp.

Используемые Assimp flags:

- `aiProcess_Triangulate` - всё превращается в треугольники.
- `aiProcess_ConvertToLeftHanded` - конвертация под left-handed coordinate system.
- `aiProcess_FlipUVs` - переворот UV.
- `aiProcess_GenNormals` - генерация normals, если их нет.
- `aiProcess_CalcTangentSpace` - tangents для normal mapping.

Алгоритм:

1. Assimp читает файл.
2. Сначала создаются материалы из `scene->mMaterials`.
3. Для каждого Assimp material читаются diffuse и normal texture пути.
4. Текстуры читаются через `LoadTexture`; относительные пути сначала проверяются рядом с файлом модели.
5. Каждый Assimp mesh конвертируется в общий `Mesh`.
6. Для каждого Assimp mesh создаётся `Submesh` с `indexOffset`, `indexCount` и `MaterialID`.
7. Готовый `Mesh` сохраняется через `CreateMesh`.

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

- diffuse fallback: `white1x1.dds`
- normal fallback: `default_nmap.dds`

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

Она создаёт `Material`, а изображения читает в CPU-память через `LoadTexture`.

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

Метод резолвит путь, читает изображение в `Texture::imageData` и возвращает `TextureID`. GPU-ресурсы здесь не создаются.

Кеш использует абсолютный нормализованный путь, поэтому повторная загрузка возвращает прежний ID без повторного чтения. Путь ищется напрямую, затем в `../../Textures`, как раньше в renderer. Исходный filename сохраняется в `Texture`.

Для DDS `DirectX::LoadDDSImageFromFile` проверяет заголовок и объём данных, сохраняя сжатие и mip-цепочку. Остальные изображения читает `stb_image`; результат — RGBA8 с одним mip-уровнем. Unicode filenames поддерживаются через открытие файла по wide-пути.

Ошибка чтения/декодирования выбрасывает `std::runtime_error` с путём. Неуспешная загрузка не попадает в кеш, поэтому после исправления файла можно повторить вызов.

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

## Lazy GPU upload mesh

Когда `RenderSystem` вызывает:

```cpp
mAdapter->DrawMesh(mesh.meshID);
```

`D3DRenderAdapter` делает:

```cpp
MeshGPU* meshGPU = GetMeshGPU(meshId);
```

Если mesh ещё не загружен на GPU:

1. `UploadMesh(meshId)` берёт CPU mesh из `ResourceManager`.
2. Создаёт default GPU vertex buffer.
3. Создаёт default GPU index buffer.
4. Создаёт vertex/index buffer views.
5. Копирует submesh metadata.
6. Сохраняет `MeshGPU` в `mGeometries`.

Это называется lazy upload: CPU mesh может быть создан заранее, но GPU buffers создаются только при первом draw.

## Lazy GPU upload material and textures

Когда renderer рисует submesh, он берёт material:

```cpp
MaterialGPU* matGPU = GetOrLoadMaterial(submesh.material);
```

Если material ещё не был создан на GPU-side:

1. CPU material читается из `ResourceManager`.
2. Создаётся `MaterialGPU`.
3. `color` превращается в `DiffuseAlbedo`.
4. `roughness` копируется в `Roughness`.
5. Если есть `albedo TextureID`, renderer получает готовый `ImageData` из `ResourceManager` и загружает его на GPU.
6. Если есть `normal TextureID`, renderer загружает его `ImageData` на GPU.
7. Если textures нет или загрузка не удалась, используются fallback textures.

GPU texture loading делает:

```cpp
D3DRenderAdapter::UploadTexture(TextureID textureId)
```

Он:

- проверяет GPU-кеш по `TextureID`;
- получает `Texture::imageData` из ResourceManager;
- создаёт GPU texture и upload buffer;
- копирует subresources через `UpdateSubresources` и переводит ресурс в `PIXEL_SHADER_RESOURCE`;
- создаёт SRV согласно размерности, mip-уровням и массиву изображения;
- сохраняет `TextureGPU`, удерживая upload buffer в живых.

Адаптер не читает файл и не декодирует изображение. Fallback `white1x1.dds` и `default_nmap.dds` также проходят через ResourceManager.

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

Для textured cube из JSON путь выглядит так:

1. `SceneSerializer::Load` читает entity.
2. `JsonToMesh` видит `"source": "primitive"`.
3. `JsonToMaterial` создаёт material.
4. `ResourceManager::LoadTexture(L"bricks2.dds")` читает DDS в `ImageData` и возвращает `TextureID`.
5. `ResourceManager::CreateMaterial` возвращает `MaterialID`.
6. `ResourceManager::CreateCube(material)` создаёт CPU mesh.
7. Entity получает `MeshComponent{ meshId }`.
8. На первом draw `D3DRenderAdapter::UploadMesh` создаёт GPU buffers.
9. На первом draw submesh `GetOrLoadMaterial` создаёт `MaterialGPU`.
10. `D3DRenderAdapter::UploadTexture` загружает готовый `ImageData` на GPU и создаёт SRV.
11. Renderer bind-ит SRV + material constant buffer и рисует.

## Текущие ограничения

- Нет unload/release API для CPU ресурсов.
- ID-генераторы static, а не поля `ResourceManager`; несколько ResourceManager в одном процессе будут делить счётчики.
- Нет cache для `LoadMesh(path)`: одна и та же модель, загруженная дважды, создаст два разных `MeshID`.
- CPU material можно создать, но изменение параметров уже загруженного `MaterialGPU` сейчас не имеет полноценного versioning/update path.
- Texture cache есть на CPU-side по нормализованному абсолютному пути и на GPU-side по `TextureID`.
- Чтение и декодирование в `ResourceManager::LoadTexture` пока синхронные; CPU-данные остаются в памяти после GPU upload.
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
