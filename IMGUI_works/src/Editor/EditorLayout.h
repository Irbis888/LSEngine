#pragma once
#include <cstdint>
#include <imgui.h>

struct EditorContext;

namespace EditorLayout
{
    void Configure(EditorContext& context);
    void DrawDockspace(EditorContext& context, float toolbarBottom);
    void Save(EditorContext& context);
    void Reset(EditorContext& context);
    const char* SettingsDirectory();
}
