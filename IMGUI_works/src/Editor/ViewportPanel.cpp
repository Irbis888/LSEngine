#include "EditorContext.h"

#include <imgui.h>

static void DrawViewportPanel(EditorContext& ctx)
{
    if (!ImGui::Begin("Viewport — 3D Preview Info", &ctx.showViewport))
    {
        ImGui::End();
        return;
    }

    const ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::TextWrapped(
        "The full-window 3D scene is drawn behind ImGui. "
        "This panel only shows mode and size — not a separate render.");
    ImGui::Text("Panel content size: %.0f x %.0f px", size.x, size.y);
    ImGui::TextDisabled("Embedded render texture: [PLACEHOLDER]");

    if (ctx.mode == EditorMode::Edit)
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Editor mode: Edit (physics off, editing on)");
    else
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Editor mode: Play (physics on, inspector locked)");

    ImGui::End();
}

void Editor_DrawViewport(EditorContext& ctx)
{
    DrawViewportPanel(ctx);
}
