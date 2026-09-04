# Try2 Engine Overview

Architectural documentation for the **Try2** DirectX 12 engine — the product built from [`Try2.sln`](../LSEngine/Chapter%209%20Texturing/Try2/Try2.sln). The project lives inside the broader [LSEngine](../LSEngine/) repository, which also contains auxiliary samples and shared assets.

## Documents

| Document | Description |
|----------|-------------|
| **[TotalOverview.md](TotalOverview.md)** | **Complete merged reference** — Parts 1–4 (overview, stack, structure, editor additions) in one file with table of contents |
| [01-project-overview.md](01-project-overview.md) | What Try2 is, lineage, capabilities, gaps, and how to build |
| [02-technologies-and-patterns.md](02-technologies-and-patterns.md) | Stack, dependencies, patterns, and `Try2/Shaders/` pipeline |
| [03-code-structure.md](03-code-structure.md) | Try2 project layout, modules, APIs, Mermaid diagrams, file index |
| [04-editor-additions.md](04-editor-additions.md) | Editor improvements: save/load, Play snapshot, debug restore, legend, stats |

## Related documentation

| Document | Description |
|----------|-------------|
| [Changes/README.md](../Changes/README.md) | Post-merge and ImGui editor — detailed architecture and data flow |
| [Changes/FromScratch-Merge-FS_w_GUI.md](../Changes/FromScratch-Merge-FS_w_GUI.md) | JSON scenes, ECS lights, F5 shader reload |
| [Changes/IMGUI-Editor-System.md](../Changes/IMGUI-Editor-System.md) | `LSEngine/IMGUI_works/`, `ImGuiBridge`, panels, Edit/Play |
| [Tasks/Editor-Additions-Plan.md](../Tasks/Editor-Additions-Plan.md) | Plan for editor items 1–8 |
| [Tasks/Editor-Debug-Restore.md](../Tasks/Editor-Debug-Restore.md) | Optional restore on Stop + Restore Snapshot button |

## Scope

| In scope | Out of scope |
|----------|--------------|
| **Product:** [`Try2/`](../LSEngine/Chapter%209%20Texturing/Try2/) and [`Try2.sln`](../LSEngine/Chapter%209%20Texturing/Try2/Try2.sln) | TexColumns application (`TexColumns.sln`) |
| **GUI (compiled):** [`LSEngine/IMGUI_works/`](../LSEngine/IMGUI_works/) via `Try2.vcxproj` | Full Dear ImGui / ImGuizmo vendor internals |
| **Scenes:** [`Try2/Scenes/`](../LSEngine/Chapter%209%20Texturing/Try2/Scenes/) (e.g. `DemoScene.json`) | |
| **Compiled deps:** `../../Common/` — `GameTimer`, `MathHelper`, `DDSTextureLoader` | Unused Luna modules in Common (`d3dApp`, `Camera`, `model`, …) |
| **Header deps:** EnTT, GLM, Assimp, nlohmann, `d3dx12.h` | Vendored library internals |
| **Auxiliary include:** `../TexColumns/CustomBuffer.h` | Duplicate `LSEngine/Shaders/` tree |

All content is in English and optimized for readability.

## Build (quick start)

1. Open **`LSEngine/Chapter 9 Texturing/Try2/Try2.sln`** in Visual Studio 2022.
2. Configuration: **Debug \| x64**.
3. Ensure `Try2/Libs/assimp-vc143-mt.lib` is present.
4. Ensure **`LSEngine/IMGUI_works/`** exists at the LSEngine repo root (required by `Try2.vcxproj`).
5. Set `AdditionalIncludeDirectories` to `$(SolutionDir)..\..\Common` if the hardcoded path in `Try2.vcxproj` does not match your machine.
6. **Optional editor:** create **`.imgui_on`** beside `Try2.exe` (e.g. `x64/Debug/.imgui_on`). Press **`~`** to toggle the UI.

After editing split docs (`01`–`04`), regenerate the merged file: `python Overview/merge_total_overview.py`.
