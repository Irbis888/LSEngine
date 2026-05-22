#include "ImGuiPlatform.h"

#include <Commons.h>
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

namespace ImGuiPlatform
{
    std::wstring GetExecutableDirectory(HINSTANCE appInst)
    {
        wchar_t path[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(appInst, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            return L".";

        PathRemoveFileSpecW(path);
        return path;
    }

    bool IsLicensed(HINSTANCE appInst)
    {
        const std::wstring dir = GetExecutableDirectory(appInst);
        const std::wstring flagPath = dir + L"\\.imgui_on";
        return PathFileExistsW(flagPath.c_str()) == TRUE;
    }

    bool IsTogglePressed(const InputState& input)
    {
        return input.keysPressed[VK_OEM_3];
    }
}
