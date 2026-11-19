# Смешивание и глубина материалов

В sample-проекте откройте **Scenes → Scenes/MaterialRenderStatesDemo.json**. Стартовая сцена проекта не изменена. Все ресурсы находятся в `Assets/MaterialRenderStatesDemo`; внешние загрузки не нужны.

```powershell
.\x64\Release\Try2.exe --scene Scenes/MaterialRenderStatesDemo.json
.\x64\Release\Try2.exe --play --scene Scenes/MaterialRenderStatesDemo.json
```

Примеры слева направо, сверху вниз:

| № | Пример | Что проверять |
| --- | --- | --- |
| 01 | Opaque | Ближний оранжевый куб скрывает дальнюю синюю сферу. |
| 02 | Masked | Решётка пишет цвет, глубину и ID только по полосам; отверстия открывают синий объект. |
| 03 | Alpha Blend | Красный, зелёный и синий слои смешиваются от дальнего к ближнему. Серый объект сзади виден через них, жёлтый спереди закрывает их. |
| 04 | Additive | Перекрытия цветных сфер складывают RGB: `S.rgb * S.a + D.rgb`. Alpha: `S.a + D.a`. |
| 05 | Depth Test Off | Бирюзовая сфера расположена за серым экраном, но видна через него. Запись глубины выключена. |
| 06 | Depth Write On | Оранжевый экран рисуется первым, записывает глубину и скрывает позднюю дальнюю синюю сферу. |
| 07 | Depth Write Off | Та же последовательность, но экран не пишет глубину: дальняя синяя сфера рисуется поверх него. |
| 08 | Greater | Зелёная сфера проходит только там, где она глубже записанной серой поверхности. Вне серого экрана она отсекается глубиной фона. |
| 09 | Test Off / Write On | Дальний красный экран безусловно заменяет глубину ближнего серого. Синяя сфера между ними затем проходит обычный Less. |

Объекты и материалы имеют номера примеров. Выберите объект в Hierarchy, нажмите **Edit material** в Inspector. В **Material Editor → Render State** выберите пресет или измените поля. Изменения видны на шарике; **Save** обновляет все назначения этого файла в сцене. **Apply to selected** назначает файл выбранной сущности. Число PSO, hits/misses доступны в Statistics.

## Данные и API

`MaterialBase::renderState` использует только общие enum из `MaterialRenderState.h`. Старые материалы получают Depth Test/Write = true, Compare = Less, Blend = false. Preset заполняет эти же поля. Ручное отклонение от пресета отображается как Custom. Surface mode Opaque/Masked управляет clip независимо от смешивания.

```cpp
const MaterialID material = resources.LoadMaterialAsset("Assets/MaterialRenderStatesDemo/AlphaBlue.lsmaterial");
MaterialRenderState state = MaterialRenderState::AlphaBlend();
state.depthCompare = DepthComparison::LessEqual;
resources.SetMaterialRenderState(material, state);
```

API вызывается в главном потоке. ID, карты, константы и использующие материал сущности сохраняются; заменяется неизменяемое описание базы. Для сохранения используйте существующий `SaveMaterialAsset(path, description)` с `description.surface->renderState = state`; `UpdateTexturedMaterial` также принимает эти поля.

В `.lsmaterial`, inline-материалах сцены и prefab поля записываются в `material.surface.renderState`:

```json
{
  "depthTest": true,
  "depthWrite": false,
  "depthCompare": "Less",
  "blendEnabled": true,
  "sourceColor": "SrcAlpha",
  "destinationColor": "InvSrcAlpha",
  "colorOperation": "Add",
  "sourceAlpha": "One",
  "destinationAlpha": "InvSrcAlpha",
  "alphaOperation": "Add"
}
```

Compare: Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always. Factors: Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DestColor, InvDestColor, DestAlpha, InvDestAlpha. Operations: Add, Subtract, ReverseSubtract, Min, Max. Неизвестные поля/enum и неправильные типы отклоняются с именем поля и файла. Все комбинации перечисленных значений определены: цветовой factor на alpha-канале означает его alpha-компонент; Min/Max игнорируют factors. При выключенном тесте Compare игнорируется; Write = true реализован как включённый D3D depth с Always.

## Порядок и выбор объектов

Обычные и masked подмеши рисуются первыми, включая непрозрачные части смешанных моделей. Террейн рисуется до прозрачных мешей. Подмеши со включённым blend идут затем, по убыванию глубины центра объекта вдоль направления камеры. Depth Test остаётся независимым и проверяет глубину непрозрачных объектов.

`RenderOrderComponent{layer}` — общий механизм явного порядка внутри каждой очереди; JSON сущности: `"renderOrder": 20`, Inspector: **Mesh → Render order**. Меньшие слои идут раньше; в одном слое действует обычная сортировка. При совпадении глубины порядок стабилизируется ID сущности и индексом подмеша. В повседневных прозрачных материалах оставляйте слой 0. Явные слои нужны для намеренно зависимых от порядка техник, как примеры 05–09; они могут переопределять обычный порядок прозрачности.

Смешивание применяется только к цвету RT0. R32_UINT RT1 получает ID последнего нарисованного фрагмента, прошедшего clip и depth test. Поэтому обычный прозрачный слой выбирается как ближайший из отрисованных, а непрозрачный объект перед ним сохраняет выбор. Alpha сама по себе не делает фрагмент проходимым для picking: даже alpha = 0 оставляет ID. Для отверстий с выбором объекта за ними используйте Masked. При нестандартных слоях или Depth Test Off выбирается итоговый нарисованный слой по тем же правилам.

Сортировка выполняется по объектам, без OIT и сортировки треугольников. Пересекающиеся прозрачные поверхности и самоперекрывающиеся меши требуют отдельного решения; для прозрачных закрытых примитивов предпочтительно не включать Two sided. Сохраняется существующий RGBA8_UNORM выход с явным sRGB encode: смешивание выполняется в выходном цветовом пространстве, без HDR и физически линейного композитинга.

## Автоматическая проверка в движке

Из корня репозитория: `tests/RunRenderStateTests.ps1 -Configuration Debug` или `Release`. Используется обычный `x64/<Configuration>/Try2.exe`; тест включается только аргументом `--render-state-smoke`.

Проверяются пиксели реального GPU-кадра девяти примеров в viewport и standalone, picking opaque/masked/alpha, 57 различных состояний (все enum и независимые флаги), повторный обход без новых PSO, константы без пересоздания PSO, `.lsmaterial` и inline round trip, сохранение порядка, живое обновление общего MaterialID, отклонение неизвестных enum, Material Editor preview и D3D12 info queue при доступном debug layer. Пути к BMP-захватам печатаются в лог. Прежние сцены и материальные сценарии проверяются отдельно `tests/RunMaterialTests.ps1`.
