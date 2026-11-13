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
}void Editor_DrawSceneGuides()
{
    EditorContext& context = *Editor_GetContext();
    CameraComponent* camera = nullptr;
    TransformComponent* cameraTransform = nullptr;
    if (!context.registry || !FindActiveCamera(*context.registry, &camera, &cameraTransform))
    {
        return;
    }
    const glm::mat4 cameraRotation = glm::yawPitchRoll(cameraTransform->rotation.y, cameraTransform->rotation.x, cameraTransform->rotation.z);
    const glm::vec3 forward = glm::vec3(cameraRotation * glm::vec4(0, 0, 1, 0));
    const glm::vec3 up = glm::vec3(cameraRotation * glm::vec4(0, 1, 0, 0));
    const glm::mat4 view = glm::lookAtLH(cameraTransform->position, cameraTransform->position + forward, up);
    const glm::mat4 projection = glm::perspectiveLH_ZO(camera->fov, camera->aspectRatio, camera->nearZ, camera->farZ);
    const glm::mat4 viewProjection = projection * view;
    const ImVec2 minimum = context.viewport.imageMinimum;
    const ImVec2 maximum = context.viewport.imageMaximum;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    auto screenPosition = [&](const glm::vec4& clipPosition)
    {
        const glm::vec3 normalized = glm::vec3(clipPosition) / clipPosition.w;
        return ImVec2(minimum.x + (normalized.x + 1) * 0.5f * (maximum.x - minimum.x),
            minimum.y + (1 - normalized.y) * 0.5f * (maximum.y - minimum.y));
    };
    auto drawSegment = [&](const glm::vec3& start, const glm::vec3& end, ImU32 color, float thickness)
    {
        glm::vec4 clipStart = viewProjection * glm::vec4(start, 1);
        glm::vec4 clipEnd = viewProjection * glm::vec4(end, 1);
        if (clipStart.z < 0 && clipEnd.z < 0)
        {
            return;
        }
        if (clipStart.z < 0)
        {
            clipStart = glm::mix(clipStart, clipEnd, clipStart.z / (clipStart.z - clipEnd.z));
        }
        else if (clipEnd.z < 0)
        {
            clipEnd = glm::mix(clipEnd, clipStart, clipEnd.z / (clipEnd.z - clipStart.z));
        }
        if (clipStart.w <= 0 || clipEnd.w <= 0)
        {
            return;
        }
        drawList->AddLine(screenPosition(clipStart), screenPosition(clipEnd), color, thickness);
    };
    if (context.showGrid)
    {
        const float gridStep = std::pow(10.0f, std::floor(std::log10(std::max(std::abs(cameraTransform->position.y), 10.0f))) - 1);
        const float centerX = std::floor(cameraTransform->position.x / gridStep) * gridStep;
        const float centerZ = std::floor(cameraTransform->position.z / gridStep) * gridStep;
        const float extent = gridStep * 30;
        for (int lineIndex = -30; lineIndex <= 30; ++lineIndex)
        {
            const float coordinateX = centerX + lineIndex * gridStep;
            const float coordinateZ = centerZ + lineIndex * gridStep;
            const ImU32 color = lineIndex % 5 == 0 ? IM_COL32(155, 165, 180, 85) : IM_COL32(155, 165, 180, 35);
            drawSegment(glm::vec3(coordinateX, 0, centerZ - extent), glm::vec3(coordinateX, 0, centerZ + extent), color, 1);
            drawSegment(glm::vec3(centerX - extent, 0, coordinateZ), glm::vec3(centerX + extent, 0, coordinateZ), color, 1);
        }
        drawSegment(glm::vec3(-extent, 0, 0), glm::vec3(extent, 0, 0), IM_COL32(235, 75, 75, 180), 2);
        drawSegment(glm::vec3(0, 0, -extent), glm::vec3(0, 0, extent), IM_COL32(75, 135, 240, 180), 2);
        drawSegment(glm::vec3(0), glm::vec3(0, gridStep * 10, 0), IM_COL32(80, 220, 110, 180), 2);
    }
    if (!context.showLightDirections)
    {
        return;
    }
    for (auto entity : context.registry->view<TransformComponent>())
    {
        if (!context.registry->any_of<DirectionalLightComponent, SpotLightComponent>(entity))
        {
            continue;
        }
        const auto& transform = context.registry->get<TransformComponent>(entity);
        const glm::vec3 direction = LightDirectionFromRotation(transform.rotation);
        const float arrowLength = glm::clamp(glm::distance(cameraTransform->position, transform.position) * 0.12f, 2.0f, 25.0f);
        const glm::vec3 tip = transform.position + direction * arrowLength;
        const glm::vec3 reference = std::abs(direction.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 side = glm::normalize(glm::cross(direction, reference));
        const ImU32 color = context.selected == entity ? IM_COL32(255, 240, 130, 255) : IM_COL32(255, 190, 65, 220);
        drawSegment(transform.position, tip, color, 3);
        drawSegment(tip, tip - direction * arrowLength * 0.22f + side * arrowLength * 0.12f, color, 3);
        drawSegment(tip, tip - direction * arrowLength * 0.22f - side * arrowLength * 0.12f, color, 3);
        const glm::vec4 clipOrigin = viewProjection * glm::vec4(transform.position, 1);
        if (clipOrigin.z >= 0 && clipOrigin.w > 0)
        {
            const ImVec2 origin = screenPosition(clipOrigin);
            drawList->AddCircle(origin, 6, color, 16, 2);
            const auto* tag = context.registry->try_get<TagComponent>(entity);
            drawList->AddText(ImVec2(origin.x + 9, origin.y + 3), color, tag ? tag->tag.c_str() : "Light");
        }
    }
}
