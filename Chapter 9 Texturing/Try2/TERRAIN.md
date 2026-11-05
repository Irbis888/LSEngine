# Terrain v1

Ordinary engine startup now opens `Engine::DefaultScenePath` in Engine.h:
`Scenes/TerrainDemo.json`. The same scene is in **Scenes > Large Terrain**.
The football game still starts through its manifest/Play.cmd.
Move the editor camera with WASD/QE, Shift for faster movement and left mouse to look.
Select **Large Terrain** in the hierarchy for its inspector. **+ Create Terrain** creates
another independent terrain entity. **Remove Terrain** removes the component and releases
its specialized resources. Source/geometry/material changes require **Apply / Rebuild**;
culling, LOD colors, wireframe, bounds and collision toggles take effect immediately.

## Ownership and publication

TerrainComponent holds serializable authoring settings only, plus an unsaved rebuild revision.
TerrainResource owns one CPU heightfield, normalized weights, layer images, chunk bounds
and small shared grid/index templates. It contains no GPU objects. TerrainSystem is an
Engine-owned specialized asset manager alongside ResourceManager: it uses its CPU decoder,
but does not put temporary terrain meshes/materials into permanent ResourceManager ID maps.
Workers never access registry, ImGui or D3D12. One low-priority terrain build at a time leaves
workers for physics/resource loading; work runs continuously over whole rows/chunks, not
an arbitrary number of chunks per rendered frame. Jobs own immutable settings and a
cancellable result. Main-thread revision/generation ownership discards stale results on
rebuild, removal and world exchange. Shutdown cancels and joins before JobSystem stops.

Admission counts active and completed CPU results, reserves estimated working data and
up to 128 MiB for layer decoding, within a 384 MiB terrain budget. A completed resource is
limited to 192 MiB. Oversized requests show an error/waiting status. Individual layer files
and decoded layers are capped at 32 MiB. This is a bounded first version, not unlimited
streaming of many enormous heightfields. A single heightfield has at most 17M samples and
16384 chunks. The default demo has 2049x2049 samples, 2048x2048 units, 64-cell chunks.

D3DRenderAdapter owns per-asset height/weight/layer textures and one vertex/index buffer
per LOD shared by every chunk. It uploads under its existing soft time/byte budget; one
image can exceed a tiny soft budget to guarantee progress. New terrain becomes visible
and available to physics only after its complete GPU upload has been recorded. The old
version remains active during preparation and on errors. No geometry rebuild/upload occurs
in steady state. Fence-retired staging and old GPU resources are collected in BeginFrame;
ten-descriptor terrain blocks are returned to a dedicated free list after retirement.
Regular engine textures/resources are not cleared. Terrain uses root constants, not one
ObjectCB/MaterialCB allocation per chunk. Ordinary mesh constant writes also check their
16384-element capacity before writing.

## Coordinates and image precision

There are `samplesX-1` by `samplesZ-1` cells. With samples (i,j):

```
local = (i * sizeX/(samplesX-1), heightOffset + sample*heightScale,
         j * sizeZ/(samplesZ-1))
world = transform.position + local * transform.scale
```

The local origin is the first sample; the demo translates it by (-1024,-40,-1024).
Only finite translation and positive scale are supported. Rotation is rejected in scene
loading, rendering, physics and height queries, and disabled in the terrain inspector.
The CPU height array contains local metres; it is uploaded unchanged as linear R32_FLOAT.
The vertex shader uses integer texel Load at exactly the CPU sample coordinates, not mip
displacement. CPU queries use the same two-triangle cell diagonal as the rendered LOD0.
Outside the X/Z footprint, TryGetHeight/TryGetNormal return false; invalid transforms also
return false. TerrainSystem queries choose the highest overlapping terrain.

Height sources:

* `flat`: constant heightOffset.
* `procedural`: seeded multiscale smooth value noise; frequency is in inverse local units.
* `heightmap`: grayscale 8-bit images decoded by existing stb_image, **16-bit grayscale PNG**
  decoded directly with stb_image's 16-bit API, or DDS R8_UNORM/R16_UNORM/R32_FLOAT.
  Sample values must be linear [0,1]; dimensions must match configured samples. RGB heights,
  compressed/color DDS heights, HDR and other 16-bit image formats are explicitly rejected.
  No 16-to-8-bit conversion occurs. Windows Unicode and asset-root-relative paths work.

## Geometry, visibility and materials

Each LOD grid samples every 1,2,4,... cells. All chunks reuse those grids. Edge chunks
clamp offsets to their actual cell extents; no heightfield copy is made per chunk.
Indices are consistently uint32_t, with bytes calculated using sizeof(uint32_t); this also
handles the largest 256-cell grid which exceeds 65535 vertices once skirts are included.
Steady-state LOD selection uses horizontal distance to a chunk's footprint, thresholds
`lodDistance * 2^level`, and multiplicative hysteresis. Triangles genuinely decrease.

Skirts run vertically from every edge sample of each LOD. Depth is at least the authored
minimum and the chunk's full height range, so its skirt overlaps the neighbouring edge
even when one side approximates a curved edge more coarsely. No neighbour LOD restriction
is needed. This masks gaps; it does not stitch a single continuous triangulation or morph
LOD transitions. Silhouettes at an outer edge can show skirts, and LOD changes can pop.
Bounds include actual sample min/max and the full skirt depth. Same-frame view/projection
planes from D3DRenderAdapter cull before root constants/draws; culling can be disabled.
The debug bounds are actual 3D chunk AABBs. Wireframe and LOD colors expose grid density.

Up to four layers blend normalized RGBA weights (zero sum selects layer 0). Optional
weight maps are linear RGBA8 at the heightfield resolution. Without a map, weights come
from height/slope. Whole-map weight coordinates are separate from material UVs; each
albedo/normal pair repeats in world units using tileSize. Defaults are white albedo,
flat tangent-space normal and layer tint/roughness. Texture mipmaps supplied in DDS are
preserved. Normal maps are interpreted as linear; RGB maps and BC5/RG8 XY maps are supported
(Z is reconstructed for XY maps). Shader normals come from height derivatives and are adjusted for nonuniform
positive scale; blended tangent normals are normalized in an orthogonal basis.

The terrain shader writes the existing forward-lit backbuffer and uses its directional,
point and spot lighting. Try2 currently has no active shadow pass; none is invented for
terrain. TexColumns' deferred pipeline and texture-distance swaps were not imported.

## Physics limits

Physics invokes heightfield contacts after integration and after the ordinary sequential
pair solver. It tests triangles of all overlapped heightfield cells for Sphere/AABB bodies.
Spheres use triangle closest points; boxes clip triangles against their full X/Z footprint
and check interior peaks and edge intersections, not just centre/corner samples. Contact
correction/impulse accounts for inverse mass, restitution and friction against static ground.
No per-cell ECS colliders or single flat terrain AABB are created. Initial physics waits
for a collidable terrain's first published version; rebuilds keep the previous version.

Collision always uses the authoritative finest-resolution surface. Distant coarse visual
LOD approximates it, so distant contact/render heights can differ until a finer LOD is
selected. CPU query/contact normals are triangle face normals, whereas shading normals are
smoothed height derivatives. This is a finite, single-valued heightfield: no caves,
overhangs, arbitrary rotation, terrain deformation, angular rigidbody simulation or CCD.
Very large bodies covering many cells make the sequential contact pass expensive; rapid
motion can still tunnel. The existing body/body solver and its correction-driven pair
requeries remain sequential. Skirts are visual seam geometry, not physical walls.

## Validation

Build Try2.sln Debug/Release x64, then run `tests/RunTerrainTests.ps1 -Configuration Debug`
or Release from the repository root. This runs the **ordinary Try2 application** with
`--terrain-smoke`, its default startup scene, real renderer, editor Play/Stop and async
scene loader. It tests large creation, rebuild, a missing map, cancellation on world swap,
scene round-trip, six rebuilds, memory retirement, physics, Stop/snapshot restoration,
removal and recreation. It scans the D3D12 debug queue for errors in Debug. It preserves
the user's editor snapshot and imgui.ini. Logs are in tests/.build/terrain-application-*/.

The same run tests height/normal transforms, outside queries, full box footprints,
16-bit grayscale PNG through a Unicode path, deterministic seeds, indices, bounds,
hysteresis and cancellation. It writes real GPU readback images (`large-terrain.bmp`,
`lod-wire-bounds.bmp`) to its own logged temporary directory. Initial demo frame
timings, build time, CPU/committed GPU bytes and peak process working set are printed.
The final small-scene timings are labelled separately. Driver allocations and allocator
caches are part of process memory; a short lifecycle test is not proof of no possible leak.

The default demo now uses the existing `../../Textures/textures/terrain_hm.dds` (1024x1024 R32_FLOAT), `terrain_diffuse.dds` and `terrain_nm.dds`. One white material layer covers the full 2048x2048 metre terrain once (`tileSize=2048`), without procedural height or tint/weight blending.

Historical measurements below used the earlier **procedural 2049x2049** demo, before the source-map change; they do not describe the current demo. Measured in the real 800x600 application on this workstation (2026-11-04), 60 steady large-scene frames:

| Build | CPU preparation | Mean frame | Longest steady frame | Longest loading frame |
| --- | ---: | ---: | ---: | ---: |
| Release | 240 ms | 5.23 ms | 8.78 ms | 18.39 ms |
| Debug | 1928 ms | 4.93 ms | 9.74 ms | 29.18 ms |

Both rendered 428 visible / 596 culled chunks and 110016 triangles in the default view.
The large asset used 39092944 CPU bytes and 42357448 committed GPU bytes. Across six
small rebuilds, GPU bytes returned to 6836936, then to zero after deletion. Peak terrain
GPU allocation during replacement was 49194384 bytes; the Release process peak working
set was 374591488 bytes. Measurements include normal application frame/presentation costs;
they are observations of these runs, not frame-time guarantees or a comparison to a
previous terrain implementation in Try2.
