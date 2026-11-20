# Материальная система Try2

Откройте **Scenes → Scenes/MaterialsDemo.json** в проекте LSEngine Samples. На верхнем ряду расположены гладкий и шероховатый диэлектрики, металл и сфера с ORM и normal map под неравномерным масштабом. Внизу видны emissive, Unlit и двусторонняя решётка с объектом за ней. Отдельный Play использует эту же сцену. Для запуска с аргументами:

```powershell
Try2.exe --scene Scenes/MaterialsDemo.json
Try2.exe --play --scene Scenes/MaterialsDemo.json
```

`MaterialsBaseline.json` сохраняет геометрию, камеру, источники света и базовые карты демо с настройками старого формата. Обе сцены используют текущую сборку. Baseline служит контрольной конфигурацией для сравнения с расширенными материалами. Замеры производительности в этой проверке не проводились.

## Описание и назначение

- `MaterialBase` хранит путь HLSL, VS/PS entry points, поддерживаемые варианты, режим, culling, render state глубины/смешивания и параметры по умолчанию.
- `MaterialParameters` хранит разрешённые значения и TextureID. Конструктор `Material(sharedBase)` копирует defaults. Несколько материалов могут ссылаться на одну базу и менять собственные параметры.
- `MaterialGPU` хранит вычисленные индексы SRV и флаги интерпретации карт. `SetMaterial` выбирает PSO и пишет константы в текущий frame resource. `DrawMesh` и `DrawSubmesh` вызывают общий `DrawGpuSubmesh`.
- `ResourceManager::CreateTexturedMaterial(MaterialDesc)` преобразует старые albedo, normal, RGB color и roughness. Старые сцены и импорт Assimp используют стандартный Lit. В JSON расширенные поля находятся в `mesh.material.surface`, а старые поля остаются на прежнем месте.
- `MaterialComponent` задаёт материал всем submesh конкретной сущности. RenderSystem передаёт MaterialID в `DrawSubmeshWithMaterial`, не меняя MeshID и материалы общей геометрии. В сцене хранится `"materialComponent": {"asset": "Assets/Materials/Material.lsmaterial"}`. Удаление компонента возвращает исходные материалы. Если файл не читается, сущность продолжает рисоваться с исходными материалами, а Inspector показывает ошибку.
- `ResourceManager::LoadMaterialAsset` переиспользует MaterialID по пути. `SaveMaterialAsset` сохраняет JSON версии 1 и обновляет закешированный материал после успешной записи файла. Все сущности с этим путём получают изменения. Файлы текстур и материалов разрешаются относительно корня проекта.

## Редактор материалов

1. Откройте **View → Material Editor**, затем **New** или **Open…**. Двойной клик по `.lsmaterial` в Asset Browser тоже открывает файл.
2. Изменяйте свойства и вращайте шарик перетаскиванием или ползунком. Просмотр использует отдельный квадратный render target и тот же D3D12-путь материалов с постоянным студийным светом. Окно можно докать.
3. Сохраните материал в проект, по умолчанию в `Assets/Materials`. Выберите сущность с мешем и нажмите **Apply to selected**. В Inspector также есть **Add material component…**, замена файла и удаление компонента.
4. Сохраните сцену, чтобы назначение осталось после перезапуска. Отдельный Play читает тот же файл материала. Черновик в редакторе обновляет только шарик, а **Save** применяет правки к существующим назначениям этого файла.
5. Для инстанса откройте сохранённый материал и нажмите **Create instance**, затем **Save As…**. У каждого параметра инстанса есть галочка: снята — значение наследуется от родителя (поле показывает его и недоступно), отмечена — значение переопределено. **Open parent** открывает родителя, **Change parent…** меняет его. Inspector у сущности с инстансом показывает «Instance of …».

## Инстансы материалов

Откройте **Scenes → Scenes/MaterialInstancesDemo.json**. Верхний ряд: базовый `Painted`, инстансы `PaintedRed`, `PaintedRedGlossy` (инстанс инстанса), `PaintedGold`, `PaintedGlow` и `BrokenParent` с отсутствующим родителем — он рисуется исходным материалом меша, ошибка видна в Inspector и логе. Нижний ряд: masked-база `Grille` и её инстанс `GrilleDense`. Все файлы лежат в `Assets/MaterialInstancesDemo`.

```json
{
  "version": 1,
  "material": {
    "name": "Painted Red Glossy",
    "parent": "Assets/MaterialInstancesDemo/PaintedRed.lsmaterial",
    "overrides": {
      "roughness": 0.12,
      "normal": ""
    }
  }
}
```

- `parent` — путь `.lsmaterial` относительно проекта. Родитель сам может быть инстансом; параметры разрешаются по цепочке base → … → instance, ближайшее переопределение побеждает.
- `overrides` принимает только параметры: `color`, `alpha`, `roughness`, `metallic`, `normalStrength`, `aoStrength`, `alphaCutoff`, `emissiveColor`, `emissiveIntensity`, `emissiveLightRange`, `uvScale`, `uvOffset` и карты `albedo`, `normal`, `orm`, `emissive`. Пустая строка в карте убирает унаследованную карту. Отсутствующее поле наследуется.
- Шейдер, entry points, `mode`, `twoSided` и `renderState` принадлежат базе. В инстансе они отклоняются с именем поля, как и неизвестные поля и неверные типы. `SetMaterialRenderState` для инстанса бросает `std::invalid_argument`.
- Инстанс хранит тот же `std::shared_ptr<const MaterialBase>`, что и база, поэтому ключ PSO совпадает: инстансы не создают пайплайнов, растёт только число hits. **View → Statistics** показывает число инстансов рядом со счётчиками PSO.
- `ResourceManager` помнит связь инстанс → родитель (`GetMaterialParent`). Сохранение базы или промежуточного инстанса, `UpdateTexturedMaterial` и `SetMaterialRenderState` пересобирают всех потомков на месте: MaterialID и назначения сохраняются, собственные переопределения потомков не затираются.
- Циклы (`A → B → A`, материал-родитель самого себя) отклоняются при загрузке с цепочкой файлов в тексте ошибки, а при сохранении — до записи файла. Отсутствующий родитель даёт ошибку вида `Instance.lsmaterial: parent '…': …: Cannot open material`.
- Inline-материалы сцены тоже могут задавать `parent` и `overrides`.

## Hot reload материалов

Правьте `.lsmaterial` в любом текстовом редакторе: изменения появляются в сцене без перезапуска и без Save в редакторе. Работает в редакторе и в окне Play под тем же переключателем **Hot reload**, что и Lua (`--no-hot-reload` выключает оба).

- `Engine` раз в 250 мс вызывает `ResourceManager::ReloadChangedMaterialAssets()`. Тот сравнивает время записи и размер каждого загруженного файла с запомненными при загрузке. Изменившийся файл перечитывается и пересобирается на месте через тот же путь, что и Save: MaterialID и назначения сохраняются, инстансы-потомки обновляются, их собственные переопределения не затираются.
- Битый файл (ошибка JSON, неизвестное поле, отсутствующий родитель, цикл) не заменяет работающую версию. Ошибка с именем файла уходит в лог, в строку статуса редактора и в Inspector у компонентов этого файла; повторно файл читается только после следующего изменения. После исправления ошибка исчезает.
- Компонент, чей файл не загрузился при открытии сцены, повторяет загрузку при каждом проходе. Если починить файл или создать недостающего родителя, материал появится сразу. В `MaterialInstancesDemo` для этого достаточно создать `Assets/MaterialInstancesDemo/DoesNotExist.lsmaterial`.
- Сохранение из Material Editor не считается внешней правкой и повторно не применяется. Открытый в Material Editor файл после внешней правки перечитывается, если в нём нет несохранённых изменений; иначе редактор предупреждает, что Save перезапишет файл.
- Проверка: `tests/RunMaterialInstanceTests.ps1` — правка базы, сломанный и исправленный файл, отсутствие повторных попыток для неизменённого битого файла, цикл через правку, собственное сохранение.

Пример поля `surface`:

```json
{
  "shader": "Shaders/Default.hlsl",
  "vertexEntry": "VS",
  "pixelEntry": "PS",
  "editorVariant": true,
  "maskedVariant": true,
  "mode": "masked",
  "twoSided": true,
  "alpha": 1.0,
  "alphaCutoff": 0.5,
  "orm": "Assets/MaterialDemo/ORM.png",
  "aoStrength": 0.8,
  "normalStrength": 1.0,
  "emissive": "Assets/MaterialDemo/Emissive.png",
  "emissiveColor": [1.0, 1.0, 1.0],
  "emissiveIntensity": 1.0,
  "uvScale": [1.0, 1.0],
  "uvOffset": [0.0, 0.0]
}
```

## Контракт HLSL

Обычные меши используют один layout: POSITION float3 на 0, NORMAL float3 на 12, TANGENT float3 на 24, TEXCOORD float2 на 36. Все атрибуты в slot 0, stride 44. `Surface.hlsli` определяет геометрию, VS, буферы и выборки. Программа определяет `ShadeSurface` и подключает `SurfaceOutput.hlsli`. Встроенные файлы Default и Unlit разрешаются из папки движка. Для остальных относительных путей сначала проверяется проект, затем папка движка. Абсолютные пути допустимы.

| Register | Содержимое |
| --- | --- |
| b0 | World, InverseWorld, ObjectUV, 192 байта |
| b1 | PassConstants с прежней раскладкой, массив Light начинается на 448, stride 48 |
| b2 | MaterialConstants, 80 байт, CBV stride 256 |
| b3 | Один DWORD EntityIdentifier для editor-варианта |
| t0, t1, t2, t3 | Base Color, Normal, ORM, Emissive, отдельные Texture2D SRV |
| s4 | Anisotropic wrap |

Строки по 16 байт в b2: Base Color RGBA; Emissive RGB и intensity; UV scale и offset; roughness, metallic, normal strength, AO strength; alpha cutoff, flags, padding. C++ проверяет размеры и смещения через static_assert. D3DReflect проверяет регистры, размеры, имена и смещения b2, допустимые входные атрибуты и выходы. Несовместимые стадии дополнительно отклоняет D3D12 при создании PSO.

PS выводит float4 SV_Target0 в RGBA8_UNORM. `EDITOR_VIEWPORT` добавляет uint SV_Target1 в R32_UINT. Оба прохода используют D24S8 и один sample. `SURFACE_MASKED` включает общий `clip(baseColor.a - AlphaCutoff)` до записи цвета, depth и ID. По умолчанию Opaque и Masked пишут глубину, blending выключен; `surface.renderState` переопределяет эти настройки. Two Sided отключает culling и переворачивает нормаль и битангенс на задней стороне. Translucent зарезервирован в enum. Террейн сохраняет собственные шейдеры, ресурсы, layout и picking.

## Формулы и цвет

- UV = ObjectUV(uv) × uvScale + uvOffset.
- Base Color = линейный RGBA factor × выборка. sRGB преобразование касается только RGB.
- Roughness = roughnessFactor × ORM.G, в BRDF минимум 0.045. Metallic = metallicFactor × ORM.B. AO = lerp(1, ORM.R, aoStrength).
- Для отсутствующей, ещё загружаемой или ошибочной ORM привязана белая карта. Явный metallicFactor действует всегда. Без заданного множителя используется 0 до получения ORM и 1 с готовой ORM. Это позволяет использовать канал B без дополнительной настройки.
- Normal map читается линейно, XY умножается на normalStrength, результат преобразуется через ортонормальный касательный базис. Используются inverse-transpose для нормали, знак UV по производным и безопасная нормализация. Для BC5/RG8 восстанавливается Z, учитывается SNORM. Допустимый normalStrength ограничен диапазоном 0..8.
- Emissive = линейный цвет × intensity × цветовая карта. Lit добавляет его после освещения. Unlit выводит Base Color + Emissive.
- `emissiveLightRange` задаёт радиус локального света в мировых единицах. По умолчанию 5, значение 0 оставляет только свечение поверхности. Источник находится в позиции сущности и использует emissiveColor × emissiveIntensity. Для меша с несколькими материалами берётся самый яркий. Кадр использует до четырёх источников по близости их зоны к камере. Они освещают Lit-меши и террейн, используя затухание `saturate(1 − d² / range²)² / (1 + d²)`. Карта используется на излучающей поверхности. Локальный свет приближён точечным источником без теней.
- Lit использует GGX, коррелированный Smith и Fresnel Schlick. F0 = lerp(0.04, baseColor, metallic). Диффузия умножается на (1 − metallic) и (1 − Fresnel). AO действует на ambient, прямой свет обрабатывает directional, point и spot. Без IBL отражения металла формируются прямыми источниками света.
- Цветовые карты используют sRGB SRV, normal/ORM используют линейный SRV. Для совместимых RGBA/BGRA и BC1/2/3/7 оба SRV ссылаются на одну typeless-текстуру. Для форматов без sRGB SRV цветовая выборка декодируется в HLSL. RGBA8_UNORM backbuffer и editor viewport получают RGB после одного явного sRGB encode. Alpha остаётся линейной. Генерация mip-уровней сохраняет существующую политику ImageIO, включая усреднение PNG/TGA в исходном UNORM.
- Пустые цветовые слоты используют белый, normal использует плоскую карту, ORM использует белый. Загрузка указанной карты сохраняет статус Loading/Failed, имя и ошибку. Base Color/Emissive используют checker до успешной загрузки, Normal/ORM используют безопасные нейтральные карты. Ошибка выводится в лог и Statistics.

## Кеш и ресурсы

Ключ включает путь программы, entry points, Opaque/Masked, Two Sided, цветовой и ID-форматы, depth format, sample count/quality и все поля depth/blend render state. Сравнение и хеш учитывают одинаковые поля. Root signature, input layout и triangle topology фиксированы для этого кеша. Варианты PS компилируются по ключу, VS переиспользуется между вариантами одной программы. Параметры, TextureID, имена материалов и entity IDs в ключе отсутствуют.

F5 строит набор новых пайплайнов и публикует его после успешной компиляции. Старые PSO и root signatures удерживаются до fence. При ошибке остаётся предыдущий набор. Для ошибочной программы при первом использовании применяется Lit с совместимым режимом и culling, диагностика кешируется до reload. SRV публикуются один раз на текстуру и остаются стабильными до уничтожения renderer. Константы записываются в защищённые fence frame resources. Изменения параметров и карт обходятся без flush. **View → Statistics** показывает созданные material PSO, hits, misses и последнюю ошибку.

## Проверка

`tests/RunMaterialTests.ps1 -Configuration Debug` и `Release` запускают обычный Try2 в editor и standalone, сохраняют реальные backbuffer-захваты и проверяют picking через отверстие и по полосе маски, кеш после изменения roughness и карт, отдельные ключи режима и программы, откат сломанного HLSL, fallback отсутствующей карты и D3D12 info queue при наличии debug layer. Дополнительно открываются baseline, TerrainDemo и MaterialInstancesDemo. Скрипт печатает пути к захватам и счётчики. Проверка также сохраняет и повторно читает материал с кириллическим именем, обновляет его по прежнему ID, проверяет сериализацию компонента, назначение и удаление через GPU picking и сохраняет кадр окна с шариком. Стандартные editor/texture проверки покрывают прежние сцены и потоковую загрузку.

`tests/RunMaterialInstanceTests.ps1` собирает отдельный тест без окна и GPU. Он проверяет разрешение цепочки из трёх уровней, общий `MaterialBase`, очистку карты пустой строкой, живое распространение правок базы и середины цепочки с сохранением переопределений, round trip файла инстанса, инстанс в памяти (как в превью редактора), а также диагностику: отсутствующий родитель, цикл при загрузке и при сохранении (файл не перезаписывается), самонаследование, неизвестное поле, поле базы в `overrides`, `surface` в инстансе, неверный тип и запрет `SetMaterialRenderState` для инстанса.

Основные изменения: ResourceManager.h/.cpp, SceneSerializer.cpp, FrameRes.h, d3dUtils.h, D3DRenderAdapter.h/.cpp, D3DSurfaceMaterials.cpp, D3DTerrain.cpp, Default.hlsl, Surface.hlsli, SurfaceOutput.hlsli, Unlit.hlsl, Try2.cpp, StatisticsPanel.cpp, сцены/Assets/MaterialDemo, файлы проекта и проверки в tests.

Управление смешиванием и глубиной, порядок прозрачности и отдельная демонстрация описаны в [MATERIAL_RENDER_STATES.md](MATERIAL_RENDER_STATES.md).
