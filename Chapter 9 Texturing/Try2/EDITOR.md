# Docked editor

The editor uses Dear ImGui **v1.92.9b-docking**, updated as one distribution from
`tests/.build/dependencies/imgui-v1.92.9b-docking.zip`. Win32/DX12 backends and all
core/internal/stb headers match. ImGuizmo is unchanged. Multi-viewports are disabled.
`imconfig.h` retains the project's math-operator option. `/utf-8` and `.editorconfig`
remain enabled. The bundled Roboto Medium includes Cyrillic; its Apache 2.0 license
and Google copyright are recorded in `IMGUI_works/Assets/Fonts/`.

## Layout and settings

Licensed editor builds open the UI immediately. The initial layout is Hierarchy
left (20%), Inspector right (25%), Viewport center and Asset Browser/Console tabs
below (25% height). Panels can be closed, moved, resized, floated inside the main
window and docked. View reopens panels; View > Reset Layout restores the defaults.
Statistics, Help and the ImGui demo are opt-in. No material-editor placeholder is opened.

`EditorLayout.cpp` isolates the DockBuilder calls. It builds only when the dock node
is absent or Reset Layout was requested. Window IDs use `###` suffixes.
Settings live in `%LOCALAPPDATA%/LSEngine/Editor/<project-path-hash>/`:

- `layout-v1.ini`: ImGui window and docking state;
- `panels-v1.json`: panel visibility.

The hash is stable FNV-1a of the canonical project root in UTF-8. This keeps user
preferences out of scenes, games and source control. Moving the project starts a
new layout. Versioned names ignore legacy undocked `imgui.ini` files.

Asset Browser lists real project folders/files, with Up navigation and double-click
scene opening. It does not promise asset import via drag/drop or an asset database.
Console shows a bounded editor-status history, current scene errors and Lua errors.
It is not a general stdout/stderr terminal.

## Frame and resource ownership

Application begins a renderer frame (existing frame-resource fence synchronization),
then begins ImGui, measures the viewport image, allocates/resizes its render texture
and builds the UI draw lists. Engine Update follows; then BeginScene binds the scene
targets, Engine Draw records meshes and terrain, and EndScene transitions the color
texture for sampling and restores the backbuffer. ImGui renders its draw lists to
the backbuffer, then the frame is submitted/presented. The Image command refers to
the same target rendered later in that command list, so there is no dependency cycle.
UI/gizmo edits are applied before the scene draw. Camera movement during a drag is
reflected by the scene immediately; the gizmo's UI projection follows on the next
UI frame during that drag (selection is disabled during camera drags).

IRenderAdapter exposes viewport resize, scene begin/end, entity draw identifiers,
and asynchronous pick requests/results. Editor panels do not manipulate D3D resources.
D3DRenderAdapter owns color RGBA8, integer ID R32_UINT and separate D24S8 depth textures.
Their viewport/scissor and camera aspect use the image dimensions, with ImGui's
framebuffer scale applied. Win32 DPI awareness and WM_DPICHANGED are handled; fonts
and spacing scale with DPI. Hidden/collapsed/zero-area viewports skip scene draws.
Standalone games and separate Play processes continue rendering to the backbuffer.

Color transitions PS_RESOURCE -> RENDER_TARGET -> PS_RESOURCE. IDs transition to
COPY_SOURCE only for a requested pixel, then return to RENDER_TARGET. Old targets,
RTV/DSV heaps and SRV slots survive until their last possible submission fence.
SRV slots 980..995 are reserved below the engine allocator (1024+) and separate
from ImGui's 900..963 range. The bounded 16-slot pool postpones a resize if every
slot is still in flight, rather than waiting or growing indefinitely. Frame-fence
collection returns slots and resources for reuse. No runtime viewport resize or
picking path flushes the GPU. Existing swapchain resize/shader-reload waits remain.

## Selection and input

The editor variants of the ordinary mesh and terrain PSOs output color and an ID
in the same depth-tested pass. There is no approximate bounding-box hit test and
no second scene traversal. All submeshes inherit their MeshComponent owner's ID;
terrain uses the actual displaced LOD and skirts. Wireframe uses the same geometry
and fill mode; debug bounds do not write IDs. Current engine materials are opaque
(no masked-material mode); the MRT wrapper calls the same shading function, so any
future alpha discard placed there affects color and ID together.

Zero means background. Each submitted readback retains its own ID-to-versioned-ECS-
entity table, request revision and scene generation. Only one pixel is copied into
a 512-byte readback allocation. Up to four requests can be in flight; completed fences
are polled without waiting. Old scene results, deleted/reused ECS handles and results
superseded by a Hierarchy/primitive/other selection are discarded. Scene registry
exchanges increment the generation, including snapshot restoration.

Short LMB release inside the image selects; background clears selection. A movement
of more than four image-coordinate pixels turns LMB into a camera drag and prevents
selection. Gizmo gestures are excluded. WASD/QE (Shift fast, Ctrl slow) operates only
with viewport hover/focus or active camera capture. Camera capture can continue
outside the image until release; focus loss, collapse/close, and release cancel it.
Gizmo uses the image rect and window draw list clipped to that rect, not the foreground
of the whole application. Hierarchy, Inspector and viewport share `EditorContext::selected`.
Edit/Play/loading restrictions remain intact.

## Verification

Build Debug/Release x64 with `Try2.sln`. `tests/RunEditorTests.ps1 -Configuration Debug`
(or Release) launches the actual app with an isolated generated project. It verifies
initial docking, coordinate conversion, depth-overlapping meshes, two imported
submeshes, a sphere's empty AABB corner, terrain IDs, background deselection, stale
Hierarchy/deletion/scene results, hidden viewport, visibility restore, layout reset,
25 consecutive resize cycles, Cyrillic scene/name round trip and separate Play/Stop.
Debug runs check the D3D12 info queue for resource/descriptor/synchronization errors.
A real frame after ImGui is captured as `ValidationOutput/editor.bmp` for inspection.
The opt-in capture test alone flushes to read its screenshot; runtime picking does not.
`--editor-preview --project <file>` records a real editor frame under the system temp
folder and checks the Debug info queue without modifying the project's scenes.

The application scenarios and GPU frames were inspected for the engine terrain demo
and MyGame0's EvilIslandsCourtyard scene. Native Computer Use capture failed twice
with capture timeouts. Therefore manual mouse dragging/docking, real DPI-monitor
transfers, physical Cyrillic keyboard entry, and focus-loss gestures are not claimed
as manually verified; their paths were implemented and reviewed. Tests use actual
renderer submissions and editor state, not synthetic screenshots or desktop automation.

Known limits: single selection; no outline or multi-OS-window docking; no material
editor, undo system or asset database. Pick results usually arrive after one or two
frames depending on GPU load. Requests use the frame rendered when the click is
submitted, not a frozen copy of an earlier image. The scene render target is capped
at 8192 pixels per axis.
