# Обзор движка Try2

Архитектурная документация для движка DirectX 12 **Try2** — продукта, собранного из [`Try2.sln`](../LSEngine/Chapter%209%20Texturing/Try2/Try2.sln). Проект находится внутри более широкого репозитория [LSEngine](../LSEngine/), который также содержит вспомогательные примеры и общие ресурсы.

## Документы

| Документ | Описание |
|----------|----------|
| **[TotalOverview.md](TotalOverview.md)** | **Полный объединённый справочник** — части 1–4 (обзор, стек, структура, дополнения редактора) в одном файле с оглавлением |
| [01-project-overview.md](01-project-overview.md) | Что такое Try2, происхождение, возможности, пробелы и сборка |
| [02-technologies-and-patterns.md](02-technologies-and-patterns.md) | Стек, зависимости, паттерны и конвейер `Try2/Shaders/` |
| [03-code-structure.md](03-code-structure.md) | Структура проекта Try2, модули, API, диаграммы Mermaid, индекс файлов |
| [04-editor-additions.md](04-editor-additions.md) | Улучшения редактора: сохранение/загрузка, снимок Play, восстановление отладки, легенда, статистика |

## Связанная документация

| Документ | Описание |
|----------|----------|
| [Changes/README.md](../Changes/README.md) | После слияния и редактор ImGui — подробная архитектура и поток данных |
| [Changes/FromScratch-Merge-FS_w_GUI.md](../Changes/FromScratch-Merge-FS_w_GUI.md) | JSON-сцены, ECS-свет, горячая перезагрузка шейдеров F5 |
| [Changes/IMGUI-Editor-System.md](../Changes/IMGUI-Editor-System.md) | `LSEngine/IMGUI_works/`, `ImGuiBridge`, панели, Edit/Play |
| [Tasks/Editor-Additions-Plan.md](../Tasks/Editor-Additions-Plan.md) | План пунктов редактора 1–8 |
| [Tasks/Editor-Debug-Restore.md](../Tasks/Editor-Debug-Restore.md) | Опциональное восстановление при Stop + кнопка Restore Snapshot |

## Область охвата

| В области | Вне области |
|----------|--------------|
| **Продукт:** [`Try2/`](../LSEngine/Chapter%209%20Texturing/Try2/) и [`Try2.sln`](../LSEngine/Chapter%209%20Texturing/Try2/Try2.sln) | Приложение TexColumns (`TexColumns.sln`) |
| **GUI (компилируется):** [`LSEngine/IMGUI_works/`](../LSEngine/IMGUI_works/) через `Try2.vcxproj` | Внутренности вендоров Dear ImGui / ImGuizmo |
| **Сцены:** [`Try2/Scenes/`](../LSEngine/Chapter%209%20Texturing/Try2/Scenes/) (напр. `DemoScene.json`) | |
| **Скомпилированные зависимости:** `../../Common/` — `GameTimer`, `MathHelper`, `DDSTextureLoader` | Неиспользуемые модули Luna в Common (`d3dApp`, `Camera`, `model`, …) |
| **Заголовочные зависимости:** EnTT, GLM, Assimp, nlohmann, `d3dx12.h` | Внутренности вендорных библиотек |
| **Вспомогательный include:** `../TexColumns/CustomBuffer.h` | Дублирующее дерево `LSEngine/Shaders/` |

Весь текст на русском языке, ориентирован на удобство чтения.

## Сборка (кратко)

1. Откройте **`LSEngine/Chapter 9 Texturing/Try2/Try2.sln`** в Visual Studio 2022.
2. Конфигурация: **Debug | x64**.
3. Убедитесь, что `Try2/Libs/assimp-vc143-mt.lib` на месте.
4. Убедитесь, что **`LSEngine/IMGUI_works/`** существует в корне репозитория LSEngine (требуется `Try2.vcxproj`).
5. Задайте `AdditionalIncludeDirectories` = `$(SolutionDir)..\..\Common`, если жёстко прописанный путь в `Try2.vcxproj` не подходит вашей машине.
6. **Опциональный редактор:** создайте **`.imgui_on`** рядом с `Try2.exe` (напр. `x64/Debug/.imgui_on`). Нажмите **`~`**, чтобы переключить UI.

После правок раздельных документов (`01`–`04`) пересоберите объединённый файл: `python ORu/merge_total_overview.py`.
