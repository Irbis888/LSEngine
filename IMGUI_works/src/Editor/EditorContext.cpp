#include "EditorContext.h"
#include "ImGuiBridge.h"
#include <Engine.h>

#include <imgui.h>
#include <ImGuizmo.h>

static EditorContext g_EditorContext;

void Editor_SetContext(EditorContext* ctx)
{
    if (ctx)
        g_EditorContext = *ctx;
}

EditorContext* Editor_GetContext()
{
    return &g_EditorContext;
}

bool Editor_IsPhysicsEnabled()
{
    if (!ImGuiBridge::IsLicensed())
        return true;
    return g_EditorContext.mode == EditorMode::Play;
}

bool Editor_CanEditComponents()
{
    return g_EditorContext.mode == EditorMode::Edit &&
        (!g_EditorContext.engine || !g_EditorContext.engine->IsSceneLoading());
}

bool Editor_WantsInputCapture()
{
    if (!ImGui::GetCurrentContext())
        return false;
    const auto& viewport = g_EditorContext.viewport;
    if (ImGuizmo::IsUsing() || ImGui::GetIO().WantTextInput)
    {
        return true;
    }
    return !viewport.cameraCaptured && !(viewport.visible && viewport.hovered && viewport.focused);
}

void Editor_SelectEntity(EditorContext& context, entt::entity entity)
{
    context.selected = entity;
    context.viewport.observedSelection = entity;
    ++context.viewport.selectionRevision;
    context.viewport.pendingPick = 0;
}

bool EditorViewportState::ScreenToPixel(ImVec2 position, uint32_t& pixelX, uint32_t& pixelY) const
{
    const float imageWidth = imageMaximum.x - imageMinimum.x;
    const float imageHeight = imageMaximum.y - imageMinimum.y;
    if (!visible || !width || !height || imageWidth <= 0 || imageHeight <= 0 ||
        position.x < imageMinimum.x || position.y < imageMinimum.y ||
        position.x >= imageMaximum.x || position.y >= imageMaximum.y)
    {
        return false;
    }
    pixelX = std::min(width - 1, uint32_t((position.x - imageMinimum.x) * width / imageWidth));
    pixelY = std::min(height - 1, uint32_t((position.y - imageMinimum.y) * height / imageHeight));
    return true;
}
