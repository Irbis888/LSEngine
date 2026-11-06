# Projects and the Play window

LSEngine keeps game content in a **project**: a folder anywhere on disk, separate from the
engine sources. The engine folder (`Chapter 9 Texturing/Try2`) keeps only engine files
(shaders, code) plus the sample project with the demos.

## Project layout

```
MyGame/
  MyGame.lsproject     project file (JSON)
  Scenes/              scenes (*.json); Scenes/Main.json is created with the project
  Assets/
    Textures/          images; textures picked outside the project are copied here
    Models/            models (.obj, ... through assimp)
  Scripts/             Lua scripts (*.lua)
  Prefabs/             entity templates (*.json) for Lua Spawn
  Temp/                generated files (Play sessions); listed in .gitignore
```

Scripts and prefabs may also live in `Games/<name>/Scripts` and `Games/<name>/Prefabs`, as
in the sample project (`Games/BallField`).

## Project file `<Name>.lsproject`

```json
{
  "formatVersion": 1,
  "name": "MyGame",
  "startupScene": "Scenes/Main.json",
  "textureSearchPaths": [ "Assets/Textures" ]
}
```

| Field | Meaning |
|---|---|
| `formatVersion` | Project format version. The engine reads `1`; a newer version is rejected with a clear error. |
| `name` | Display name: editor toolbar, window titles. |
| `startupScene` | Scene opened by the editor and by `--play`, relative to the project folder. File > Set as Startup Scene changes it. |
| `textureSearchPaths` | Extra folders (relative to the project) searched for textures given by a bare file name, for example `"tile.dds"` in a scene or a texture name inside a model's `.mtl` file. The project folder itself is always searched first. |

Unknown fields are preserved when the editor rewrites the file. The folder of the project
file is the **asset root**: every relative path in scenes, scripts, prefabs, terrain
settings and models is resolved against it (`AssetPaths`, `ResourceManager::SetAssetRoot`,
`SceneSerializer`, `ScriptRuntime`, `TerrainSystem`). When a scene is saved, paths of files
inside the project are written relative to it (`Assets/Textures/rock.png`), so the project
folder can be moved or copied. Files outside the project keep their path as written.

Shaders are engine files: they are always loaded from the engine folder, whatever project is
open.

## Creating and opening projects

In the editor (toggle the UI with `~`; requires `.imgui_on` next to `Try2.exe`):

- **File > New Project...** — name and location (Browse opens the native folder dialog). The
  editor creates `<Location>/<Name>/` with the standard folders, the project file and a starter
  scene (camera, sun, ground plane), then restarts with the new project.
- **File > Open Project...** — native folder dialog; choose a folder that contains a
  `.lsproject` file. The editor restarts with that project.
- **File > New Scene... / Open Scene... / Save Scene (Ctrl+S) / Save Scene As...** — native
  dialogs inside the project. Scenes must be inside the project folder.
- **Scenes** menu — every scene of the project (`Scenes/**`, `Games/*/Scenes/**`).
- **Inspector > Material** — color, roughness, albedo and normal textures of a mesh. Browse
  imports an image from outside the project into `Assets/Textures`.

Opening another project restarts the editor process: the resource caches, the Lua state and
the terrain system all belong to one asset root, and a fresh process is the simplest way to
guarantee nothing from the previous project leaks into the next one. Save the scene first.

Command line:

```
Try2.exe                                    editor with the sample project (engine demos)
Try2.exe --project <folder|file.lsproject>  editor with a project
Try2.exe --new-project <folder> [--name N]  create a project (outside the engine folder) and open it
Try2.exe --play --project <p> [--scene S]   game window for a scene (default: startupScene)
         [--no-hot-reload]
Try2.exe [--project <p>] --game <game.json> standalone game from a manifest (Games/BallField)
--show-editor                               start the editor with its UI visible (otherwise ~ toggles it)
```

## Sample project (compatibility)

The existing demos are the project `Samples.lsproject` in the engine folder. It is opened when
no `--project` is given, so `Scenes/*.json`, `Games/BallField`, `PlayBallField.cmd` and the
tests work as before. Its `textureSearchPaths` contains `../../Textures`, which replaces the
old hard-coded `../../Textures` fallback of `ResourceManager`; models are referenced as
`../../Common/*.obj` relative to the project folder.

## Play in a separate window

**Play** in the editor toolbar runs the game in its own window. The editor stays in Edit mode.

How it works:

1. The editor writes the current scene — including unsaved edits — to
   `<project>/Temp/Play/<Scene>.json` (portable form: no session mesh ids).
2. It starts `Try2.exe --play --project <file> --scene Temp/Play/<Scene>.json` in a job object
   (the game cannot outlive the editor).
3. The game process loads the scene, starts Lua scripts and physics, and renders into its own
   window and swap chain, with its own input and camera (fly camera with the left mouse
   button held, as before; scripts may move the camera). The title shows
   `<Project> | Play: <Scene> | Esc closes` once Play has started, or the load error.
4. **Stop** in the editor sends `WM_CLOSE` to the game window (terminating it after 3 s if it
   does not exit). Closing the game window or pressing Esc in it has the same effect. The
   editor notices the exit and shows "Play stopped".

Because the game never touches the editor's registry, nothing has to be restored on Stop: the
editor scene is exactly as it was before Play, and edits made while the game runs are kept
(they are used by the next Play).

### Why a separate process

The alternative — a second window with its own swap chain inside the editor process — would
need a second `World`/registry, a second `ScriptSystem` (Lua state), physics and terrain
system, renderer support for several swap chains and per-window input routing. Several parts
of the engine are process-wide today (`Application` is a singleton, the editor context and
ImGui backend are global, the renderer owns one swap chain, resource ids come from global
counters). The engine already had a standalone game mode (`--game`), so a separate process
gives full isolation (a crashing game script cannot take the editor down) at the cost of
loading the scene's resources again in the game process.

### In-editor simulation

The previous in-process Play still exists as **Debug > Simulate in Editor**: scripts and
physics run on the editor scene, and Stop restores `Scenes/EditorPlaySnapshot.json`. The
automated tests (`--lua-smoke`, `--terrain-smoke`, `ScriptEngineTests`) use it.

## Tests

`tests/RunProjectSmoke.ps1` runs the whole workflow through the real editor process in a
temporary folder outside the repository (with spaces and Cyrillic in the path): create a
project, add a scene, import a PNG, save, reload, Play in a window, Stop; reopen the project
from its file; run `--play` on its own.
