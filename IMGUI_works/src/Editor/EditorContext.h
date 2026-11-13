#pragma once

#include <entt/entt.hpp>
#include <string>
#include <cstdint>
#include <imgui.h>

class Engine;
class ResourceManager;
struct FrameContext;

enum class EditorMode
{
    Edit,
    Play
};

enum class GizmoOperation
{
    Translate,
    Rotate,
    Scale
};

struct EditorViewportState
{
    ImVec2 imageMinimum = {};
    ImVec2 imageMaximum = {};
    uint32_t width = 0;
    uint32_t height = 0;
    bool visible = false;
    bool hovered = false;
    bool focused = false;
    bool cameraCaptured = false;
    bool pointerDown = false;
    bool pointerDragged = false;
    ImVec2 pointerStart = {};
    uint64_t selectionRevision = 0;
    uint64_t pendingPick = 0;
    uint64_t sceneGeneration = 0;
    entt::entity observedSelection = entt::null;

    bool ScreenToPixel(ImVec2 position, uint32_t& pixelX, uint32_t& pixelY) const;
};

struct EditorContext
{
    Engine* engine = nullptr;
    entt::registry* registry = nullptr;
    ResourceManager* resources = nullptr;

    entt::entity selected = entt::null;
    EditorMode mode = EditorMode::Edit;
    GizmoOperation gizmoOp = GizmoOperation::Translate;
    bool gizmoLocal = true;

    bool showGrid = true;
    bool showLightDirections = true;
    bool showHierarchy = true;
    bool showInspector = true;
    bool showViewport = true;
    bool showAssets = true;
    bool showConsole = true;
    bool showStatistics = false;
    bool showLegend = false;
    bool showDemo = false;
    bool showPrimitiveCreator = false;


    EditorViewportState viewport;
    std::string statusMessage;
};

void Editor_SetContext(EditorContext* ctx);
EditorContext* Editor_GetContext();
bool Editor_IsPhysicsEnabled();
bool Editor_CanEditComponents();
bool Editor_WantsInputCapture();

void Editor_BeginFrame(const FrameContext& context);
void Editor_RenderUI(const FrameContext& context);
void Editor_RenderGizmo();
void Editor_DrawLegend(EditorContext& ctx);

void Editor_SelectEntity(EditorContext& context, entt::entity entity);

void Editor_DrawSceneGuides();
