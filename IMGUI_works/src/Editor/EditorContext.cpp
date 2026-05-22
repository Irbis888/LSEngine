#include "EditorContext.h"
#include "ImGuiBridge.h"

#include <imgui.h>

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
    return g_EditorContext.mode == EditorMode::Edit;
}

bool Editor_WantsInputCapture()
{
    if (!ImGui::GetCurrentContext())
        return false;
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}
