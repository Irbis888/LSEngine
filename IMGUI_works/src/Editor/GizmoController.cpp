#define GLM_ENABLE_EXPERIMENTAL

#include "EditorContext.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <Commons.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <cstring>

static glm::mat4 BuildWorldMatrix(const TransformComponent& t)
{
    glm::mat4 m(1.0f);

    m = glm::translate(m, t.position);

    m = glm::rotate(m, t.rotation.z, glm::vec3(0, 0, 1));
    m = glm::rotate(m, t.rotation.y, glm::vec3(0, 1, 0));
    m = glm::rotate(m, t.rotation.x, glm::vec3(1, 0, 0));

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

void Editor_RenderGizmo()
{


    EditorContext& ctx = *Editor_GetContext();

    if (!ctx.registry || ctx.selected == entt::null || !ctx.registry->valid(ctx.selected))
        return;

    if (!ctx.registry->all_of<TransformComponent>(ctx.selected))
        return;

    if (!Editor_CanEditComponents() || !ctx.viewport.visible)
        return;

    CameraComponent* camera = nullptr;
    TransformComponent* cameraTr = nullptr;

    if (!FindActiveCamera(*ctx.registry, &camera, &cameraTr))
        return;

    ImGuizmo::SetOrthographic(false);


    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

    const ImGuiIO& io = ImGui::GetIO();

    ImGuizmo::SetRect(
        ctx.viewport.imageMinimum.x,
        ctx.viewport.imageMinimum.y,
        ctx.viewport.imageMaximum.x - ctx.viewport.imageMinimum.x,
        ctx.viewport.imageMaximum.y - ctx.viewport.imageMinimum.y
    );

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;

    switch (ctx.gizmoOp)
    {
    case GizmoOperation::Rotate:
        op = ImGuizmo::ROTATE;
        break;

    case GizmoOperation::Scale:
        op = ImGuizmo::SCALE;
        break;

    case GizmoOperation::Translate:
    default:
        op = ImGuizmo::TRANSLATE;
        break;
    }

    const ImGuizmo::MODE mode =
        ctx.gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    auto& transform = ctx.registry->get<TransformComponent>(ctx.selected);

    glm::mat4 objectMatrix = BuildWorldMatrix(transform);

    const float aspect =
        camera->aspectRatio > 0.0f
        ? camera->aspectRatio
        : io.DisplaySize.x / io.DisplaySize.y;

    const glm::mat4 cameraRotation = glm::yawPitchRoll(cameraTr->rotation.y, cameraTr->rotation.x, cameraTr->rotation.z);
    const glm::vec3 forward = glm::vec3(cameraRotation * glm::vec4(0, 0, 1, 0));
    const glm::vec3 up = glm::vec3(cameraRotation * glm::vec4(0, 1, 0, 0));
    const glm::vec3 eye = cameraTr->position;
    const glm::mat4 view = glm::lookAtLH(eye, eye + forward, up);

    const glm::mat4 proj =
        glm::perspectiveLH_ZO(
            camera->fov,
            aspect,
            camera->nearZ,
            camera->farZ
        );

    float objectM16[16];
    std::memcpy(objectM16, glm::value_ptr(objectMatrix), sizeof(objectM16));

    if (ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        op,
        mode,
        objectM16))
    {
        glm::mat4 result;
        std::memcpy(glm::value_ptr(result), objectM16, sizeof(objectM16));

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