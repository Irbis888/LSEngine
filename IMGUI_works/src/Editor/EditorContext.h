#pragma once

#include <entt/entt.hpp>
#include <string>

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

struct EditorContext
{
    Engine* engine = nullptr;
    entt::registry* registry = nullptr;
    ResourceManager* resources = nullptr;

    entt::entity selected = entt::null;
    EditorMode mode = EditorMode::Edit;
    GizmoOperation gizmoOp = GizmoOperation::Translate;
    bool gizmoLocal = true;

    bool showHierarchy = true;
    bool showInspector = true;
    bool showViewport = true;
    bool showStatistics = true;
    bool showLegend = true;
    bool showDemo = false;
    bool showPrimitiveCreator = false;

    /// Debug: when true, Stop reloads Scenes/EditorPlaySnapshot.json. When false, Stop only exits Play mode.
    bool restoreSceneOnStop = false;

    std::string statusMessage;
};

void Editor_SetContext(EditorContext* ctx);
EditorContext* Editor_GetContext();
bool Editor_IsPhysicsEnabled();
bool Editor_CanEditComponents();
bool Editor_WantsInputCapture();

void Editor_BeginFrame(const FrameContext& context);
void Editor_RenderUI(const FrameContext& context);
void Editor_RenderGizmo(const FrameContext& context);
void Editor_DrawLegend(EditorContext& ctx);
