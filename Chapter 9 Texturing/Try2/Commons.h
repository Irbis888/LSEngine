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

#include "PhysicsCommons.h"

using MeshID = uint32_t;
using MaterialID = uint32_t;
using TextureID = uint32_t;


struct InputState
{
    // is_pressed 
    bool keys[256] = {};

	// is_just_pressed 
    bool keysPressed[256] = {};
    bool keysReleased[256] = {};

    // мышь
    bool mouseButtons[8] = {};
    bool mouseButtonsPressed[8] = {};
    bool mouseButtonsReleased[8] = {};

    float mouseWheelDelta = 0.0f;

    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;

    int mouseX = 0;
    int mouseY = 0;

    bool firstMouse = true;
};


struct FrameContext
{
    GameTimer& timer;
    InputState input;

    float physDT;
};



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

struct DirectionalLightComponent
{
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    glm::vec3 direction = glm::normalize(glm::vec3(-0.6f, -0.7f, 0.2f));
    bool enabled = true;
};

struct PointLightComponent
{
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float falloffStart = 1.0f;
    float falloffEnd = 25.0f;
    bool enabled = true;
};

struct SpotLightComponent
{
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    glm::vec3 direction = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));
    float falloffStart = 1.0f;
    float falloffEnd = 35.0f;
    float spotPower = 32.0f;
    bool enabled = true;
};

struct DirectionalLightData
{
    glm::vec3 strength = glm::vec3(1.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
};

struct PointLightData
{
    glm::vec3 strength = glm::vec3(1.0f);
    glm::vec3 position = glm::vec3(0.0f);
    float falloffStart = 1.0f;
    float falloffEnd = 25.0f;
};

struct SpotLightData
{
    glm::vec3 strength = glm::vec3(1.0f);
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    float falloffStart = 1.0f;
    float falloffEnd = 35.0f;
    float spotPower = 32.0f;
};

constexpr int RenderDirectionalLightCount = 1;
constexpr int RenderPointLightCount = 2;
constexpr int RenderSpotLightCount = 3;

struct SceneLightData
{
    glm::vec4 ambient = glm::vec4(0.25f, 0.25f, 0.28f, 1.0f);
    std::vector<DirectionalLightData> directionalLights;
    std::vector<PointLightData> pointLights;
    std::vector<SpotLightData> spotLights;
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
    virtual bool ReloadShaders() = 0;
    virtual void SetTimeData(float TotalTime, float DeltaTime) = 0;

    virtual void SetTransform(const TransformComponent& world) = 0;
    virtual void SetCamera(const CameraComponent& camera, const TransformComponent& transform) = 0;
    virtual void SetLights(const SceneLightData& lights) = 0;
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

	virtual void Update(entt::registry& reg, const FrameContext& context) {}
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
