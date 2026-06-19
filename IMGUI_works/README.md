# IMGUI_works

Isolated Dear ImGui + ImGuizmo + Try2 editor UI. Merge into Try2 with minimal engine diffs.

## Layout

- `ThirdParty/imgui` — Dear ImGui + Win32/DX12 backends
- `ThirdParty/ImGuizmo` — gizmo library
- `src/` — bridge and editor panels

## Enable editor at runtime

1. Build Try2 from `LSEngine/Chapter 9 Texturing/Try2/Try2.sln`
2. Create empty file `.imgui_on` next to `Try2.exe` (same folder as the executable)
3. Press `~` (US keyboard, `VK_OEM_3`) to show/hide the editor overlay

Without `.imgui_on`, ImGui is not initialized.

## Try2.vcxproj paths

From `Try2/` (`Chapter 9 Texturing/Try2/`): Common is `$(SolutionDir)..\..\Common`, IMGUI root is `$(SolutionDir)..\..\IMGUI_works` (i.e. `LSEngine/IMGUI_works/`).

Include directories:

- `$(IMGUI_WORKS)src`
- `$(IMGUI_WORKS)ThirdParty\imgui`
- `$(IMGUI_WORKS)ThirdParty\imgui\backends`
- `$(IMGUI_WORKS)ThirdParty\ImGuizmo`

## Updating vendors

```powershell
cd d:\neiroslopland\Downloads
git -C imgui pull
git -C ImGuizmo pull
# recopy into IMGUI_works/ThirdParty and update VERSIONS.txt
```
