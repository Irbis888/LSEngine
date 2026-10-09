# GPU Mesh Upload Infrastructure - Implementation Guide

## Overview

Imported mesh requests use the bounded resource upload pump in `BeginFrame`. Generated planes, cubes, and spheres upload lazily on their first draw, with copies and barriers recorded before that draw in the same command list. See `RESOURCE_MANAGER.md` for CPU jobs, placeholders, upload budgets, and shutdown. `GetMeshGPU` returns `nullptr` while an imported mesh is pending.

## Architecture

### Key Components

1. **MeshGPU Struct** (from `d3dUtils.h`)
   - `vertexBuffer`: GPU vertex buffer resource (D3D12)
   - `indexBuffer`: GPU index buffer resource (D3D12)
   - `vbView`: Vertex buffer view for binding to command list
   - `ibView`: Index buffer view for binding to command list
   - `indexCount`: Number of indices for draw calls

2. **Upload Functions** (in `D3DRenderAdapter`)
   - `SetResourceManager(ResourceManager* rm)`: Must call once after creating adapter to link mesh data
   - `UploadMesh(MeshID meshId)`: Records GPU upload commands and caches the buffers (idempotent; requires an open command list)
   - `GetMeshGPU(MeshID meshId)`: Returns cached buffers, uploads a CPU-ready primitive immediately, or queues another CPU-ready mesh for a later BeginFrame (returns nullptr while pending)

## Usage Pattern

### Step 1: Link ResourceManager to Adapter (Once, after Init)

```cpp
// After creating D3DRenderAdapter and ResourceManager
D3DRenderAdapter adapter;
ResourceManager resourceMgr;

adapter.Init(hwnd, width, height);

// Link them
adapter.SetResourceManager(&resourceMgr);
```

### Step 2a: Explicit Upload (Upload Known Meshes)

```cpp
// After ResourceManager has loaded mesh data
MeshID sponzaMeshId = resourceMgr.LoadMesh("Assets/Sponza.gltf");

// Explicit tools/tests path: a command list must be open.
adapter.BeginFrame();
MeshGPU* gpuMesh = adapter.UploadMesh(sponzaMeshId);
// gpuMesh can be nullptr while an asynchronous CPU import is pending.
adapter.EndFrame();

// Use gpuMesh->vbView and gpuMesh->ibView in draw calls
```

### Step 2b: Queue on Demand

```cpp
// Called during render pass for each entity
MeshID entityMeshId = entity.meshComponent.meshId;

// Queues if not already uploaded; finalization runs in BeginFrame
MeshGPU* gpuMesh = adapter.GetMeshGPU(entityMeshId);
if (!gpuMesh) return; // Try again after the next BeginFrame upload pump.

// Bind and draw
mCommandList->IASetVertexBuffers(0, 1, &gpuMesh->vbView);
mCommandList->IASetIndexBuffer(&gpuMesh->ibView);
mCommandList->DrawIndexedInstanced(gpuMesh->indexCount, 1, 0, 0, 0);
```

## How It Works

### Upload Pipeline

1. **Fetch CPU Data**: Get vertex/index arrays from ResourceManager via MeshID
2. **Create GPU Buffers**: 
   - Uses `d3dUtils::CreateDefaultBuffer()` to create GPU default buffers
   - Staging: Upload buffers are stored in MeshGPU until its uploadCompleteFence
3. **Create Views**: 
   - Vertex buffer view with correct stride (sizeof(Vertex) = 44 bytes)
   - Index buffer view with DXGI_FORMAT_R32_UINT format
4. **Store**: MeshGPU struct stored in `mGeometries` map with MeshID as key
5. **Lifecycle**: CleanupMeshUploadBuffers releases staging buffers once the GPU fence completes; normal frames do not flush for each upload

### Vertex Format (44 bytes stride)

```cpp
struct Vertex {
    glm::vec3 Position;     // 12 bytes
    glm::vec3 Normal;       // 12 bytes
    glm::vec3 TangentU;     // 12 bytes
    glm::vec2 TexC;         // 8 bytes
};
```

This matches the input layout defined in `BuildShadersAndInputLayout()`.

## Storage Location

**When to upload meshes:**

- **NOT at adapter Init()**: ResourceManager may not have loaded meshes yet
- **After ResourceManager is ready**: Call `SetResourceManager()` first
- **Queued upload strategy**: Call `GetMeshGPU()` when rendering an entity
  - First request for a CPU-ready primitive: Records the upload and returns usable buffers for this draw
  - First request for another CPU-ready mesh: Queues upload and returns nullptr
  - A subsequent BeginFrame processes queued meshes within its upload budget
  - Subsequent entities with an uploaded mesh receive cached GPU buffers

## Integration with Draw Calls

Update `DrawIndexed()` or your render system to:

```cpp
MeshGPU* mesh = adapter.GetMeshGPU(entityMeshId);
if (mesh) {
    mCommandList->IASetVertexBuffers(0, 1, &mesh->vbView);
    mCommandList->IASetIndexBuffer(&mesh->ibView);
    mCommandList->DrawIndexedInstanced(mesh->indexCount, 1, 0, 0, 0);
}
```

## Key Design Decisions

1. **On-demand Uploads**: Primitives upload on first draw; other meshes are finalized in BeginFrame
   - Supports async resource loading
   - No upfront cost for unused meshes

2. **Idempotent**: `UploadMesh()` safe to call multiple times
   - Returns cached GPU resource if already uploaded
   - Cached geometry is reused

3. **Resource Ownership**: 
   - MeshGPU stored in `mGeometries` map
   - Upload buffers kept alive in MeshGPU until its fence completes

4. **Error Handling**:
   - Throws if ResourceManager not set
   - Returns nullptr while CPU import is pending or failed
   - Throws if a CPU-ready mesh has no vertices/indices
   - Propagates D3D12 errors via `ThrowIfFailed()`

## Next Steps

1. Call `SetResourceManager()` in your Engine/Application init code
2. Load meshes: `resourceMgr.LoadMesh("path")` → returns MeshID
3. In render loop, call `adapter.GetMeshGPU(meshId)` and bind vertices/indices
4. Test rendering the Sponza mesh
