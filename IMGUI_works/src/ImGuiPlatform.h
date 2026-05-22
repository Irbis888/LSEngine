#pragma once

#include <Windows.h>
#include <string>

struct InputState;

namespace ImGuiPlatform
{
    bool IsLicensed(HINSTANCE appInst);
    bool IsTogglePressed(const InputState& input);
    std::wstring GetExecutableDirectory(HINSTANCE appInst);
}
