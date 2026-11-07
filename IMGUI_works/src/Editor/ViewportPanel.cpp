#include "EditorContext.h"
#include <Engine.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <Windows.h>
#include <algorithm>

void Editor_DrawViewport(EditorContext& context)
{
    auto& viewport = context.viewport;
    viewport.visible = false;
    viewport.hovered = false;
    viewport.focused = false;
    const bool gizmoEnabled = context.showViewport && Editor_CanEditComponents() &&
        context.registry && context.selected != entt::null && context.registry->valid(context.selected) &&
        context.registry->all_of<TransformComponent>(context.selected) && !ImGui::GetIO().AppFocusLost;
    ImGuizmo::Enable(gizmoEnabled);
    if (!context.showViewport || !context.engine)
    {
        viewport.cameraCaptured = false;
        viewport.pointerDown = false;
        return;
    }
    if (!ImGui::Begin("Viewport###Viewport", &context.showViewport, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        viewport.cameraCaptured = false;
        viewport.pointerDown = false;
        ImGuizmo::Enable(false);
        ImGui::End();
        return;
    }
    const ImVec2 imageSize = ImGui::GetContentRegionAvail();
    if (imageSize.x < 1 || imageSize.y < 1)
    {
        viewport.cameraCaptured = false;
        viewport.pointerDown = false;
        ImGuizmo::Enable(false);
        ImGui::End();
        return;
    }
    const ImVec2 pixelScale = ImGui::GetIO().DisplayFramebufferScale;
    viewport.width = std::clamp(uint32_t(imageSize.x * pixelScale.x), 1u, 8192u);
    viewport.height = std::clamp(uint32_t(imageSize.y * pixelScale.y), 1u, 8192u);
    auto& renderer = context.engine->GetRenderer();
    const uint64_t texture = renderer.ResizeEditorViewport(viewport.width, viewport.height);
    viewport.imageMinimum = ImGui::GetCursorScreenPos();
    ImGui::Image(ImTextureID(texture), imageSize);
    viewport.imageMaximum = ImGui::GetItemRectMax();
    viewport.visible = texture != 0;
    viewport.hovered = ImGui::IsItemHovered();
    viewport.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    for (auto entity : context.registry->view<CameraComponent>())
    {
        context.registry->get<CameraComponent>(entity).aspectRatio = float(viewport.width) / viewport.height;
    }
    ImGui::GetWindowDrawList()->PushClipRect(viewport.imageMinimum, viewport.imageMaximum, true);
    Editor_RenderGizmo();
    ImGui::GetWindowDrawList()->PopClipRect();

    const ImVec2 mousePosition = ImGui::GetMousePos();
    const bool gizmoActive = gizmoEnabled && (ImGuizmo::IsOver() || ImGuizmo::IsUsing());
    if (viewport.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoActive)
    {
        viewport.pointerDown = true;
        viewport.pointerDragged = false;
        viewport.pointerStart = mousePosition;
        ImGui::SetWindowFocus();
    }
    const float movementX = mousePosition.x - viewport.pointerStart.x;
    const float movementY = mousePosition.y - viewport.pointerStart.y;
    if (viewport.pointerDown && movementX * movementX + movementY * movementY > 16)
    {
        viewport.pointerDragged = true;
    }
    if (gizmoActive && viewport.pointerDown && !viewport.pointerDragged)
    {
        viewport.pointerDown = false;
    }
    viewport.cameraCaptured = viewport.pointerDown && viewport.pointerDragged && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (viewport.pointerDown && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        uint32_t pixelX = 0;
        uint32_t pixelY = 0;
        if (!viewport.pointerDragged && viewport.hovered && !gizmoActive &&
            !context.engine->IsSceneLoading() && viewport.ScreenToPixel(mousePosition, pixelX, pixelY))
        {
            viewport.pendingPick = ++viewport.selectionRevision;
            renderer.RequestViewportPick(pixelX, pixelY, viewport.pendingPick, context.engine->GetSceneGeneration());
        }
        viewport.pointerDown = false;
        viewport.cameraCaptured = false;
    }
    if (ImGui::GetIO().AppFocusLost || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        viewport.cameraCaptured = false;
        viewport.pointerDown = false;
    }
    ImGui::End();
}
