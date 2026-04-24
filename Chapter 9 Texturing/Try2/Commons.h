#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif


#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cassert>
#include <iostream>
#include <GameTimer.h>
#include <windows.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

using MeshID = uint32_t;
using MaterialID = uint32_t;
using TextureID = uint32_t;

struct TransformComponent
{
	glm::vec3 position;
	glm::vec3 rotation;
	glm::vec3 scale;
};
struct TagComponent
{
	std::string tag;
};
struct MeshComponent
{
	MeshID meshID;
};
struct CameraComponent
{
    float fov;           // Field of view in degrees
    float nearZ;         // Near plane distance
    float farZ;          // Far plane distance
    float aspectRatio;   // Width/Height ratio

    // Cached matrices (updated by CameraControlSystem)
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
};





class IRenderAdapter
{
public:
    IRenderAdapter() = default;
    virtual ~IRenderAdapter() = default;

    virtual void Init(void* windowHandle, uint32_t width, uint32_t height) = 0;
    virtual void SetResourceManager(class ResourceManager* resourceManager) = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void SetTimeData(float TotalTime, float DeltaTime) = 0;

    virtual void SetTransform(const TransformComponent& world) = 0;
    virtual void SetCamera(const CameraComponent& camera, const TransformComponent& transform) = 0;
    virtual void UpdCB() = 0;

    virtual void SetMaterial(MaterialID material) = 0;

    virtual void DrawIndexed(
        uint32_t indexCount,
        uint32_t startIndex,
        int32_t baseVertex
    ) = 0;

    virtual void DrawMesh(MeshID meshId) = 0;
    virtual void DrawSubmesh(MeshID meshId, uint32_t submeshIndex) = 0;
    virtual void OnResize(int width, int height) = 0;


};

class ISystem
{
public:
	//ISystem();
	virtual ~ISystem() = default;

	virtual void Update(entt::registry& reg, const GameTimer& gt) {}
};


class DxException
{
public:
    DxException() = default;
    DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

    std::wstring ToString()const;

    HRESULT ErrorCode = S_OK;
    std::wstring FunctionName;
    std::wstring Filename;
    int LineNumber = -1;
};
