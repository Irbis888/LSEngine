# GPU Mesh Upload Infrastructure - Implementation Guide

## Overview

You now have a **lazy-loading mesh upload system** that bridges CPU mesh data (from `ResourceManager`) to GPU resources (D3D12 buffers).

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
   - `UploadMesh(MeshID meshId)`: Synchronously uploads mesh to GPU (idempotent - safe to call multiple times)
   - `GetMeshGPU(MeshID meshId)`: Convenience function that auto-uploads if not already uploaded (lazy load)

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

// Upload to GPU when needed (e.g., in first frame or on-demand)
MeshGPU* gpuMesh = adapter.UploadMesh(sponzaMeshId);

// Use gpuMesh->vbView and gpuMesh->ibView in draw calls
```

### Step 2b: Lazy Load (Automatic on-demand)

```cpp
// Called during render pass for each entity
MeshID entityMeshId = entity.meshComponent.meshId;

// Automatically uploads if not already uploaded
MeshGPU* gpuMesh = adapter.GetMeshGPU(entityMeshId);

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
   - Staging: Upload buffers are created internally and kept alive in `mOwnedResources`
3. **Create Views**: 
   - Vertex buffer view with correct stride (sizeof(Vertex) = 44 bytes)
   - Index buffer view with DXGI_FORMAT_R32_UINT format
4. **Store**: MeshGPU struct stored in `mGeometries` map with MeshID as key
5. **Lifecycle**: Upload buffers kept alive until GPU work completes via `FlushCommandQueue()`

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
- **Lazy load strategy (recommended)**: Call `GetMeshGPU()` when rendering an entity
  - First entity with mesh: Triggers upload
  - Subsequent entities with same mesh: Returns cached GPU resource
  - Zero overhead after first use

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

1. **Lazy Loading**: Meshes uploaded on-demand, not at Init
   - Supports async resource loading
   - No upfront cost for unused meshes

2. **Idempotent**: `UploadMesh()` safe to call multiple times
   - Returns cached GPU resource if already uploaded
   - No re-uploads or GPU stalls

3. **Resource Ownership**: 
   - MeshGPU stored in `mGeometries` map
   - Upload buffers kept alive in `mOwnedResources` until GPU completes

4. **Error Handling**:
   - Throws if ResourceManager not set
   - Throws if mesh is empty (no vertices/indices)
   - Propagates D3D12 errors via `ThrowIfFailed()`

## Next Steps

1. Call `SetResourceManager()` in your Engine/Application init code
2. Load meshes: `resourceMgr.LoadMesh("path")` → returns MeshID
3. In render loop, call `adapter.GetMeshGPU(meshId)` and bind vertices/indices
4. Test rendering the Sponza mesh
