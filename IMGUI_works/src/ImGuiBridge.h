#pragma once

#include <Windows.h>

struct InputState;
struct FrameContext;
class D3DRenderAdapter;
class Engine;

namespace ImGuiBridge
{
    void SetAppInstance(HINSTANCE inst);
    void SetEngine(Engine* engine);

    void OnApplicationInit(HWND hwnd, D3DRenderAdapter* adapter);
    void OnApplicationShutdown();
    void OnResize();

    bool WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void ProcessInput(const InputState& input);
    void BeginFrame(const FrameContext& context);
    void RenderEditorUI(const FrameContext& context);
    void RenderOverlay(D3DRenderAdapter* adapter);

    bool IsLicensed();
    bool IsVisible();
    bool WantsCaptureInput();
}
