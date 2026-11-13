# Материальная система Try2

Откройте **Scenes → Scenes/MaterialsDemo.json** в проекте LSEngine Samples. На верхнем ряду расположены гладкий и шероховатый диэлектрики, металл и сфера с ORM и normal map под неравномерным масштабом. Внизу видны emissive, Unlit и двусторонняя решётка с объектом за ней. Отдельный Play использует эту же сцену. Для запуска с аргументами:

```powershell
Try2.exe --scene Scenes/MaterialsDemo.json
Try2.exe --play --scene Scenes/MaterialsDemo.json
```

`MaterialsBaseline.json` сохраняет геометрию, камеру, источники света и базовые карты демо с настройками старого формата. Обе сцены используют текущую сборку. Baseline служит контрольной конфигурацией для сравнения с расширенными материалами. Замеры производительности в этой проверке не проводились.

## Описание и назначение

- `MaterialBase` хранит путь HLSL, VS/PS entry points, поддерживаемые варианты, режим, culling и параметры по умолчанию.
- `MaterialParameters` хранит разрешённые значения и TextureID. Конструктор `Material(sharedBase)` копирует defaults. Несколько материалов могут ссылаться на одну базу и менять собственные параметры.
- `MaterialGPU` хранит вычисленные индексы SRV и флаги интерпретации карт. `SetMaterial` выбирает PSO и пишет константы в текущий frame resource. `DrawMesh` и `DrawSubmesh` вызывают общий `DrawGpuSubmesh`.
- `ResourceManager::CreateTexturedMaterial(MaterialDesc)` преобразует старые albedo, normal, RGB color и roughness. Старые сцены и импорт Assimp используют стандартный Lit. В JSON расширенные поля находятся в `mesh.material.surface`, а старые поля остаются на прежнем месте.
- Точки для будущего редактора на главном потоке: `CreateMaterial(Material(sharedBase))`, `GetMaterial(id).parameters`, замена `GetMaterial(id).base`, `SetMeshMaterial` и `SetSubmeshMaterial`. Для сохранения текущий редактор обновляет исходный `MeshSourceComponent` с `MaterialDesc`. Изменение только runtime-параметров через API действует до загрузки сцены. Самостоятельные ресурсы материалов с общими ссылками между сценами, редактор полного набора параметров и система наследования инстансов остаются следующим этапом.

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

PS выводит float4 SV_Target0 в RGBA8_UNORM. `EDITOR_VIEWPORT` добавляет uint SV_Target1 в R32_UINT. Оба прохода используют D24S8 и один sample. `SURFACE_MASKED` включает общий `clip(baseColor.a - AlphaCutoff)` до записи цвета, depth и ID. Opaque и Masked пишут глубину, blending выключен. Two Sided отключает culling и переворачивает нормаль и битангенс на задней стороне. Translucent зарезервирован в enum. Террейн сохраняет собственные шейдеры, ресурсы, layout и picking.

## Формулы и цвет

- UV = ObjectUV(uv) × uvScale + uvOffset.
- Base Color = линейный RGBA factor × выборка. sRGB преобразование касается только RGB.
- Roughness = roughnessFactor × ORM.G, в BRDF минимум 0.045. Metallic = metallicFactor × ORM.B. AO = lerp(1, ORM.R, aoStrength).
- Для отсутствующей, ещё загружаемой или ошибочной ORM привязана белая карта. Явный metallicFactor действует всегда. Без заданного множителя используется 0 до получения ORM и 1 с готовой ORM. Это позволяет использовать канал B без дополнительной настройки.
- Normal map читается линейно, XY умножается на normalStrength, результат преобразуется через ортонормальный касательный базис. Используются inverse-transpose для нормали, знак UV по производным и безопасная нормализация. Для BC5/RG8 восстанавливается Z, учитывается SNORM. Допустимый normalStrength ограничен диапазоном 0..8.
- Emissive = линейный цвет × intensity × цветовая карта. Lit добавляет его после освещения. Unlit выводит Base Color + Emissive.
- Lit использует GGX, коррелированный Smith и Fresnel Schlick. F0 = lerp(0.04, baseColor, metallic). Диффузия умножается на (1 − metallic) и (1 − Fresnel). AO действует на ambient, прямой свет обрабатывает directional, point и spot. Без IBL отражения металла формируются прямыми источниками света.
- Цветовые карты используют sRGB SRV, normal/ORM используют линейный SRV. Для совместимых RGBA/BGRA и BC1/2/3/7 оба SRV ссылаются на одну typeless-текстуру. Для форматов без sRGB SRV цветовая выборка декодируется в HLSL. RGBA8_UNORM backbuffer и editor viewport получают RGB после одного явного sRGB encode. Alpha остаётся линейной. Генерация mip-уровней сохраняет существующую политику ImageIO, включая усреднение PNG/TGA в исходном UNORM.
- Пустые цветовые слоты используют белый, normal использует плоскую карту, ORM использует белый. Загрузка указанной карты сохраняет статус Loading/Failed, имя и ошибку. Base Color/Emissive используют checker до успешной загрузки, Normal/ORM используют безопасные нейтральные карты. Ошибка выводится в лог и Statistics.

## Кеш и ресурсы

Ключ включает путь программы, entry points, Opaque/Masked, Two Sided, цветовой и ID-форматы, depth format, sample count/quality. Root signature, input layout, triangle topology и depth/blend настройки фиксированы для этого кеша. Варианты PS компилируются по ключу, VS переиспользуется между вариантами одной программы. Параметры, TextureID, имена материалов и entity IDs в ключе отсутствуют.

F5 строит набор новых пайплайнов и публикует его после успешной компиляции. Старые PSO и root signatures удерживаются до fence. При ошибке остаётся предыдущий набор. Для ошибочной программы при первом использовании применяется Lit с совместимым режимом и culling, диагностика кешируется до reload. SRV публикуются один раз на текстуру и остаются стабильными до уничтожения renderer. Константы записываются в защищённые fence frame resources. Изменения параметров и карт обходятся без flush. **View → Statistics** показывает созданные material PSO, hits, misses и последнюю ошибку.

## Проверка

`tests/RunMaterialTests.ps1 -Configuration Debug` и `Release` запускают обычный Try2 в editor и standalone, сохраняют реальные backbuffer-захваты и проверяют picking через отверстие и по полосе маски, кеш после изменения roughness и карт, отдельные ключи режима и программы, откат сломанного HLSL, fallback отсутствующей карты и D3D12 info queue при наличии debug layer. Дополнительно открываются baseline и TerrainDemo. Скрипт печатает пути к захватам и счётчики. Стандартные editor/texture проверки покрывают прежние сцены и потоковую загрузку.

Основные изменения: ResourceManager.h/.cpp, SceneSerializer.cpp, FrameRes.h, d3dUtils.h, D3DRenderAdapter.h/.cpp, D3DSurfaceMaterials.cpp, D3DTerrain.cpp, Default.hlsl, Surface.hlsli, SurfaceOutput.hlsli, Unlit.hlsl, Try2.cpp, StatisticsPanel.cpp, сцены/Assets/MaterialDemo, файлы проекта и проверки в tests.
