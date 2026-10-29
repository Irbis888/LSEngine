# Lua scripting v1

The engine embeds **Lua 5.5.1**, built from the unmodified official sources as a static
Visual Studio dependency in Debug/Release x64. No installed Lua, Lua DLL, LuaJIT, Luau,
sol2 or additional models are required. Sources, SHA256, build details and the MIT notice
are in `external/lua/VERSION.md`, `external/lua/LICENSE.txt`, and `external/lua/lua-5.5.1/`.

The direct Lua C API is sufficient for this small interface and avoids another binding
dependency. Lua itself is compiled as C++, which upstream supports; protected errors use
C++ unwinding so temporary C++ objects are not skipped by longjmp. Include its core API
headers directly with the matching library linkage. Upstream references:
[download/build](https://www.lua.org/download.html),
[Lua 5.5 reference](https://www.lua.org/manual/5.5/manual.html),
[license](https://www.lua.org/license.html).

## Open the demonstration

Build `Try2.sln`, Debug or Release x64, and run `x64/<Configuration>/Try2.exe`.
If the editor is hidden, press **~** (the existing editor toggle). Select
**Scenes → Lua Scripting Demo**, wait for loading, then press **Play**.

* Blue/orange cubes run the same `Scripts/Rotate.lua` with different speed, axis and label.
  Their file-local variables, environment globals, instance tables and parameters are independent.
* Arrow keys move the yellow cube using frame dt.
* J/L set the red dynamic body's horizontal velocity; Space requests one impulse.
* O makes the controller send an `Open` event; the blue door slides upward.
* X requests deletion of the purple cube. Its OnDestroy runs before component removal.
* The green FlyingSphere runs `Scripts/FlyInCircle.lua`: x = center.x + radius*cos(angle),
  z = center.z + radius*sin(angle), y = center.y; angle advances by angularSpeed*dt.
  Its center/altitude, radius and angular speed (radians/second) are editable in the inspector.

Click the viewport before using gameplay keys; editor-captured input is suppressed using
the same `ImGuiBridge::WantsCaptureInput()` policy as the existing camera controller.
WASD remains the editor camera input, so the demo deliberately uses other movement keys.
Stop calls OnDestroy and restores the snapshot, including the deleted purple cube.

## File format and attaching a script

Create a UTF-8 text file under the asset root's `Scripts/` folder. UTF-8 BOM is accepted.
Each file returns one table; callbacks are optional:

```lua
local ticks = 0 -- a fresh upvalue for each entity using this file
return {
    parameters = {
        speed = 1.0,
        axis = Vec3(0, 1, 0),
        active = true,
        label = "example"
    },
    OnInit = function(self)
        self.elapsed = 0
        Log(self.entity:GetName(), self.params.label)
    end,
    OnUpdate = function(self, dt)
        ticks = ticks + 1
        self.elapsed = self.elapsed + dt
        if self.params.active then
            self.entity:SetRotation(self.entity:GetRotation() + self.params.axis * (dt * self.params.speed))
        end
    end,
    OnFixedUpdate = function(self, dt)
        -- Fixed-step work. No repeated key-edge events here.
    end,
    OnEvent = function(self, name, payload, sender)
        Log(name, sender:IsValid())
    end,
    OnDestroy = function(self)
        Log(self.entity:GetName(), "cleanup")
    end
}
```

Select an entity in the hierarchy and use **Inspector → Add Lua Script**. Enter
`Scripts/MyScript.lua`, then **Reload description**. The inspector shows declared bool,
number, string and Vec3 fields. Edited fields become serialized overrides. Script enabled
can be toggled in Play; file, overrides, removal and Reload are Edit-only. The inspector
shows current state and the latest protected runtime/metadata error. An invalid parameter
name/type is reported; **Reset parameter overrides** clears obsolete overrides explicitly.

Reload refreshes the declaration cache; it does not migrate arbitrary Lua runtime state.
Reading metadata executes the file's top-level chunk with an instruction limit and a fresh
environment without scene access. It does not call OnInit/Update/FixedUpdate/Destroy/Event.
Keep top-level code declarative; scene access belongs in callbacks.

## Ownership, lifecycle and frame order

`ScriptComponent.h` contains only `path`, `enabled` and a typed parameter map.
`ScriptSystem.h/.cpp` is a genuine `ISystem`, owned by Engine's update-system list.
`ScriptRuntime.h/.cpp` owns exactly one interpreter for that engine and provides loading,
API registration, protected calls, metadata and command queues. No Lua work is submitted
to JobSystem. Public runtime operations reject calls from a different thread.

Each entity loads its file separately into a fresh `_ENV` and receives a fresh returned
instance table. Standard-library tables are private; math also gets its own random-generator
userdata so randomseed in one instance cannot change another's sequence. The instance
receives `self.entity` and `self.params`; arbitrary fields such as `self.elapsed` are private
runtime state. Internal Lua registry references/functions are never scene data.

OnInit runs once before the instance's other callbacks. Initially disabled components are
initialized lazily when first enabled. Disabling an initialized instance pauses update,
fixed update and events while keeping its state; re-enabling does not repeat OnInit.
Normal teardown runs OnDestroy once, including cleanup of faulted instances if they have
a valid returned table. Missing callbacks are allowed. A failed load has no instance to destroy.

The existing application order is preserved:

1. Capture the existing InputState and run zero or more actual fixed steps.
2. Before **each** physics step: synchronize script instances, call OnFixedUpdate with the
   actual `context.physDT`, then apply deferred deletions. All Lua work finishes before
   PhysicsSystem dispatches workers. Physics waits for its workers before returning.
3. Begin the render frame; Engine publishes scene work and runs camera update, then
   ScriptSystem::Update with `GameTimer::DeltaTime()`. Each OnUpdate reads completed physics
   results. Transform changes are visible to the ensuing Draw in this frame.
4. Apply deletions requested by updates, deliver one event-queue snapshot, then apply
   deletions requested by handlers. Events created during delivery wait until next Update.
5. Draw the scene, render the editor, present and clear one-frame input flags.

Key-down is available in callbacks. **Pressed/Released return true only during OnUpdate**,
never during repeated fixed substeps, init, events or destruction. Store one-shot intent
in self during OnUpdate and consume it in OnFixedUpdate, as PhysicsMover.lua does. Since
the application's fixed steps precede Update, that intent applies on the next fixed step.
dt is seconds; normal Update dt and fixed dt intentionally have different semantics.

Play begins only after the editor successfully saves `Scenes/EditorPlaySnapshot.json`.
Stop halts callbacks, performs cleanup with the old world intact, clears commands/events
and restores the snapshot asynchronously. The old optional "keep changes on Stop" debug
mode was removed to meet this lifecycle contract. BeginPlay is rejected during restoration.
If Stop occurs during another scene load, restoration is queued behind it and no scripts
start on the intermediate scene. Restore errors are shown; scripts remain stopped.

On a successful scene switch, OnDestroy runs **before** registry swap/retirement. Signals
are disconnected before the swap and connected to the new registry storage afterwards.
ScriptSystem creates the exposed Tag/Transform/Rigidbody pools before ScriptComponent;
EnTT's reverse pool-removal order also preserves them during direct entity destroy/clear
on these engine-owned registries. Engine shutdown tears down scripts before resources/world.
A scene read/parse failure leaves the old world and its paused instances intact; they resume
when the failed request ends. During Play, a newly published scene starts scripts only
after complete publication. Serialization/parsing on workers touches only plain component data.

## Entity, Vec3, input and physics API

`self.entity` is opaque userdata containing an ECS ID **with version** and a world/session
generation. Every access checks that generation, registry validity and required component.
The API exposes no registry, raw ECS pointer or arbitrary component construction. Entity
and Vec3 metatables cannot be obtained or edited through their normal Lua interface.
Deleted/stale references report a script error; `IsValid()` safely returns false.

```lua
local other = Entity.FindByName("Door") -- nil if absent; use unique tags
if other and other:IsValid() then
    Log(other:GetName())
end
local p = self.entity:GetPosition() -- COPY, not a component proxy
p.x = p.x + 1                    -- changes only the copy
self.entity:SetPosition(p)        -- explicit write back
```

Entity methods:

| Method | Semantics |
|---|---|
| `IsValid()` | Checks current scene/session and ECS version |
| `GetName()` | Reads TagComponent; missing tag is an error |
| `GetPosition()` / `SetPosition(Vec3)` | Copies/sets world position |
| `GetRotation()` / `SetRotation(Vec3)` | Copies/sets Euler angles in **radians** |
| `GetScale()` / `SetScale(Vec3)` | Copies/sets scale |
| `HasRigidbody()` | Checks for RigidbodyComponent |
| `GetVelocity()` / `SetVelocity(Vec3)` | Reads/sets existing body's linear velocity |
| `ApplyImpulse(Vec3)` | For Dynamic bodies, velocity += impulse × InverseMass(); returns bool applied |
| `SendEvent(name, payload)` | Queues an event; false for absent/disabled ScriptComponent |
| `Destroy()` | Defers entity deletion to a safe main-thread boundary |

The existing rendering/camera convention is left-handed: +X right, +Y up, zero-rotation
camera forward +Z; Euler components are pitch X, yaw Y, roll Z. Existing physics ignores
collider rotation; scripting does not change that limitation or the solver formulas.
Position setters teleport; use velocity/impulses for dynamic movement. No ApplyForce,
raycast, collision/trigger callbacks or universal entity factory are advertised.

`Vec3()` or `Vec3(x,y,z)` produces a value with writable x/y/z. Addition, subtraction,
unary minus and scalar multiplication in either order are supported. Vectors and numeric
API inputs must be finite and fit the relevant C++ representation.

`Input.IsKeyDown(Key.Left)`, `Input.IsKeyPressed(Key.Space)` and
`Input.IsKeyReleased(Key.Space)` read InputState, never a separate keyboard polling path.
Constants: A–Z, Space, Enter, Left/Right/Up/Down, Delete, Shift and Escape.
`Log(...)`/`print(...)` write prefixed messages to the application's log/console.

## Events and destruction

```lua
local target = Entity.FindByName("Door")
if target then target:SendEvent("Open", {speed=2.0, offset=Vec3(0,3,0), enabled=true, label="door"}) end
```

OnEvent receives `(self, eventName, payload, sender)`. Payload is a copied, flat table
with string keys and bool/number/string/Vec3 values; later mutation of the source table
or vector does not change the queued event. Nested tables, functions, arbitrary userdata
and entity references in payload are rejected in v1. Sender is supplied separately as a
safe reference and may already be invalid when delivered. Disabled/deleted/closed-scene
targets are discarded. Delivery is nonrecursive, one snapshot per game Update; an event
sent by OnEvent waits for the next pass. Queues are cleared on Stop or scene teardown.

Destroy never alters registry storage during a callback/view traversal. The system snapshots
commands, calls OnDestroy with components still present, then destroys the entity before
physics/next event stage. Duplicate requests are harmless. Commands generated by a normal
OnDestroy wait for a later boundary; teardown-generated commands/events are discarded.
For C++ callers, prefer `ScriptSystem::DestroyEntity` when explicitly removing a live entity.

## Scene data and paths

```json
"script": {
  "path": "Scripts/Rotate.lua",
  "enabled": true,
  "parameters": {
    "speed": {"type":"number", "value":2.0},
    "axis": {"type":"Vec3", "value":[0,1,0]},
    "label": {"type":"string", "value":"orange"}
  }
}
```

Only overrides are stored; absent entries use the file's declared defaults. Unsupported
types, unknown overrides and changed parameter types produce explicit errors. Old scenes
without script entries load unchanged. Runtime fields, closures and IDs are not serialized.

The asset root is the existing project directory containing Scenes and Shaders. AssetPaths
finds it relative to the executable (walking ancestors/project candidates), or Engine accepts
an explicit root for embedding/tests. Engine stores that root; later CWD changes do not affect
scene/script resolution. All script paths are canonical relative UTF-8 paths inside Scripts/,
including Windows Unicode filenames. Traversal/symlink paths outside that directory are rejected.
Files are opened through std::filesystem's native Windows paths; Lua compiles the loaded bytes
with the canonical UTF-8 source name for diagnostics. The loader accepts text only.

## Error handling and limits

Protected calls report file, entity name/ID, callback and Lua traceback when available.
One fault disables that runtime instance until the next Play; it does not change serialized
enabled or stop other instances. Errors do not repeat every frame. Reload outside Play or
fix parameters, then start a new Play session. The inspector displays metadata/runtime errors.

Each file evaluation and callback has a **200,000 Lua-instruction** budget, checked every
100 instructions. `ScriptRuntime::SetInstructionLimit(100..10000000)` changes it. The hook
is removed and its state cleared after success or error. It cannot interrupt a blocked or
expensive C/C++ API/library call. Limits: 1 MiB source, 64 parameter/payload entries, 128-byte
names, 4096-byte strings, 4096 queued events/deletions. No os/io/debug/package/require,
native-module loading, load/dofile/loadfile, coroutine, pcall/xpcall or bytecode loading is
exposed. This deliberately restricted API is **not a security sandbox** for hostile code.

## Validation

* `tests/RunScriptEngineTests.ps1 -Configuration Debug` (or Release): native Engine,
  real D3D12 frames and actual EditorSceneIO Play/Stop handlers. Checks isolation, parameters,
  dt, fixed ordering and one-shot input, capture policy, events/copies, deletion/cleanup,
  stale references, static/dynamic impulse behavior, Unicode, syntax/missing/runtime errors,
  instruction hooks and asynchronous scene restoration.
* `tests/RunLuaApplicationSmoke.ps1 -Configuration Debug` (or Release): launches the
  **ordinary Try2.exe** with `--lua-smoke`. Exercises Application's normal frame loop,
  actual editor initialization, renderer, Lua demo and Play/Stop/Play/Stop, starting from
  an unrelated directory. It restores the user's snapshot/ImGui settings after the run.
  `--lua-smoke` is opt-in validation; normal launches do not execute the driver.
* Existing physics, resources and JobSystem checks remain independent of Lua callbacks;
  no script is dispatched to a worker and no physics formulas were changed.

