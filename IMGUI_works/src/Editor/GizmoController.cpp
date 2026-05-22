#define GLM_ENABLE_EXPERIMENTAL
#include "EditorContext.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <Commons.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <cstring>

static glm::mat4 BuildWorldMatrix(const TransformComponent& t)
{
    glm::mat4 m(1.0f);
    m = glm::translate(m, t.position);
    m = glm::rotate(m, t.rotation.x, glm::vec3(1, 0, 0));
    m = glm::rotate(m, t.rotation.y, glm::vec3(0, 1, 0));
    m = glm::rotate(m, t.rotation.z, glm::vec3(0, 0, 1));
    m = glm::scale(m, t.scale);
    return m;
}

static void DecomposeMatrixToTransform(const glm::mat4& m, TransformComponent& t)
{
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::quat orientation;
    glm::decompose(m, t.scale, orientation, t.position, skew, perspective);

    const glm::vec3 euler = glm::eulerAngles(orientation);
    t.rotation = euler;
}

static bool FindActiveCamera(entt::registry& reg, CameraComponent** outCam, TransformComponent** outTr)
{
    for (auto entity : reg.view<TransformComponent, CameraComponent>())
    {
        *outCam = &reg.get<CameraComponent>(entity);
        *outTr = &reg.get<TransformComponent>(entity);
        return true;
    }
    return false;
}

void Editor_RenderGizmo(const FrameContext& context)
{
    (void)context;
    EditorContext& ctx = *Editor_GetContext();
    if (!ctx.registry || ctx.selected == entt::null || !ctx.registry->valid(ctx.selected))
        return;
    if (!ctx.registry->all_of<TransformComponent>(ctx.selected))
        return;
    if (ctx.mode != EditorMode::Edit)
        return;

    CameraComponent* camera = nullptr;
    TransformComponent* cameraTr = nullptr;
    if (!FindActiveCamera(*ctx.registry, &camera, &cameraTr))
        return;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();

    const ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    switch (ctx.gizmoOp)
    {
    case GizmoOperation::Rotate: op = ImGuizmo::ROTATE; break;
    case GizmoOperation::Scale: op = ImGuizmo::SCALE; break;
    default: break;
    }

    const ImGuizmo::MODE mode = ctx.gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    auto& transform = ctx.registry->get<TransformComponent>(ctx.selected);
    glm::mat4 objectMatrix = BuildWorldMatrix(transform);

    const float aspect = camera->aspectRatio > 0.0f
        ? camera->aspectRatio
        : (ImGui::GetIO().DisplaySize.x / ImGui::GetIO().DisplaySize.y);

    const glm::vec3 forward(
        cosf(cameraTr->rotation.x) * sinf(cameraTr->rotation.y),
        -sinf(cameraTr->rotation.x),
        cosf(cameraTr->rotation.x) * cosf(cameraTr->rotation.y));

    const glm::vec3 eye = cameraTr->position;
    const glm::vec3 target = eye + glm::normalize(forward);
    const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspectiveLH_ZO(
        glm::radians(camera->fov),
        aspect,
        camera->nearZ,
        camera->farZ);

    const float* viewPtr = glm::value_ptr(view);
    const float* projPtr = glm::value_ptr(proj);
    float objectM16[16];
    memcpy(objectM16, glm::value_ptr(objectMatrix), sizeof(objectM16));

    if (ImGuizmo::Manipulate(viewPtr, projPtr, op, mode, objectM16))
    {
        glm::mat4 result;
        memcpy(glm::value_ptr(result), objectM16, sizeof(objectM16));
        DecomposeMatrixToTransform(result, transform);
    }
}

void Editor_DrawGizmoToolbar(EditorContext& ctx)
{
    const bool translate = ctx.gizmoOp == GizmoOperation::Translate;
    const bool rotate = ctx.gizmoOp == GizmoOperation::Rotate;
    const bool scale = ctx.gizmoOp == GizmoOperation::Scale;

    if (ImGui::RadioButton("Translate##gizmo", translate))
        ctx.gizmoOp = GizmoOperation::Translate;
    ImGui::SetItemTooltip("Move (T). Drag arrows in the 3D view.");

    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate##gizmo", rotate))
        ctx.gizmoOp = GizmoOperation::Rotate;
    ImGui::SetItemTooltip("Rotate (R). Drag rings in the 3D view.");

    ImGui::SameLine();
    if (ImGui::RadioButton("Scale##gizmo", scale))
        ctx.gizmoOp = GizmoOperation::Scale;
    ImGui::SetItemTooltip("Scale (S). Drag scale handles.");

    ImGui::SameLine();
    ImGui::Checkbox("Local space##gizmo", &ctx.gizmoLocal);
    ImGui::SetItemTooltip("When checked, gizmo uses object axes. Off = world axes.");
}
