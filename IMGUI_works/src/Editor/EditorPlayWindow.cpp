#include "EditorPlayWindow.h"
#include "EditorContext.h"

#include <Engine.h>
#include <ScriptSystem.h>
#include <AssetPaths.h>

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <vector>

namespace
{
    PROCESS_INFORMATION g_Process{};
    HANDLE g_Job = nullptr;
    bool g_Stopping = false;
    std::chrono::steady_clock::time_point g_StopRequested;
    std::string g_SessionScene;
    constexpr auto kCloseTimeout = std::chrono::seconds(3);

    std::vector<HWND> ProcessWindows(DWORD processId)
    {
        struct Search { DWORD id; std::vector<HWND> windows; } search{ processId, {} };
        EnumWindows([](HWND window, LPARAM data) -> BOOL
        {
            auto& s = *reinterpret_cast<Search*>(data);
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == s.id && GetWindow(window, GW_OWNER) == nullptr) s.windows.push_back(window);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.windows;
    }

    void CloseHandles()
    {
        if (g_Process.hThread) CloseHandle(g_Process.hThread);
        if (g_Process.hProcess) CloseHandle(g_Process.hProcess);
        g_Process = {};
        if (g_Job) CloseHandle(g_Job); // Kill-on-close: nothing is left running.
        g_Job = nullptr;
        g_Stopping = false;
    }

    std::wstring Quote(const std::wstring& value) { return L"\"" + value + L"\""; }
}

bool EditorPlayWindow::IsRunning() { return g_Process.hProcess != nullptr; }
unsigned long EditorPlayWindow::ProcessId() { return g_Process.dwProcessId; }
const std::string& EditorPlayWindow::SessionScene() { return g_SessionScene; }

bool EditorPlayWindow::Start(EditorContext& ctx)
{
    if (IsRunning())
    {
        Focus();
        ctx.statusMessage = "Play window is already running.";
        return false;
    }
    if (!ctx.engine) { ctx.statusMessage = "Play failed: engine not bound."; return false; }
    if (ctx.mode != EditorMode::Edit) { ctx.statusMessage = "Play failed: stop the in-editor simulation first."; return false; }
    if (ctx.engine->IsSceneLoading()) { ctx.statusMessage = "Play failed: wait until the scene has loaded."; return false; }
    const auto& project = ctx.engine->GetProject();
    if (project.file.empty()) { ctx.statusMessage = "Play failed: the asset folder has no .lsproject file."; return false; }

    // The game gets the edited state, saved or not. Temp/ is a generated project folder.
    std::string stem = "Untitled";
    if (!ctx.engine->GetScenePath().empty())
        stem = AssetPaths::ToUtf8(AssetPaths::FromUtf8(ctx.engine->GetScenePath()).stem());
    g_SessionScene = "Temp/Play/" + stem + ".json";
    std::string error;
    size_t runtimeOnly = 0;
    if (!ctx.engine->SaveScene(g_SessionScene, error, false, &runtimeOnly, true))
    {
        ctx.statusMessage = "Play failed (scene): " + error;
        return false;
    }

    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
    if (!length || length >= executable.size()) { ctx.statusMessage = "Play failed: executable path unknown."; return false; }
    executable.resize(length);
    std::wstring command = Quote(executable) + L" --play --project " + Quote(project.file.wstring()) +
        L" --scene " + Quote(AssetPaths::FromUtf8(g_SessionScene).wstring());
    if (!ctx.engine->GetScripts().IsHotReloadEnabled()) command += L" --no-hot-reload";

    g_Job = CreateJobObjectW(nullptr, nullptr);
    if (g_Job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_Job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    STARTUPINFOW startup{ sizeof(startup) };
    const auto workingDirectory = AssetPaths::EngineRoot().wstring(); // Shaders are engine files.
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
        nullptr, workingDirectory.c_str(), &startup, &g_Process))
    {
        g_Process = {};
        if (g_Job) CloseHandle(g_Job);
        g_Job = nullptr;
        ctx.statusMessage = "Play failed: cannot start the game process (error " + std::to_string(GetLastError()) + ").";
        return false;
    }
    if (g_Job) AssignProcessToJobObject(g_Job, g_Process.hProcess);
    AllowSetForegroundWindow(g_Process.dwProcessId);
    ResumeThread(g_Process.hThread);
    ctx.statusMessage = "Play window started (" + g_SessionScene + ", pid " + std::to_string(g_Process.dwProcessId) + ").";
    if (runtimeOnly)
        ctx.statusMessage += " " + std::to_string(runtimeOnly) + " meshes without a source are not in the game.";
    return true;
}

void EditorPlayWindow::Stop(EditorContext& ctx)
{
    if (!IsRunning()) return;
    const auto windows = ProcessWindows(g_Process.dwProcessId);
    for (HWND window : windows) PostMessageW(window, WM_CLOSE, 0, 0);
    if (windows.empty()) TerminateProcess(g_Process.hProcess, 1); // Still starting: no window to close.
    g_Stopping = true;
    g_StopRequested = std::chrono::steady_clock::now();
    ctx.statusMessage = "Stopping the Play window...";
}

void EditorPlayWindow::Update(EditorContext& ctx)
{
    if (!IsRunning()) return;
    if (WaitForSingleObject(g_Process.hProcess, 0) == WAIT_OBJECT_0)
    {
        DWORD code = 0;
        GetExitCodeProcess(g_Process.hProcess, &code);
        const bool requested = g_Stopping;
        CloseHandles();
        if (requested || code == 0)
            ctx.statusMessage = "Play stopped. The editor scene is unchanged (Edit mode).";
        else
            ctx.statusMessage = "Play window exited with code " + std::to_string(code) +
                ". The editor scene is unchanged.";
        return;
    }
    if (g_Stopping && std::chrono::steady_clock::now() - g_StopRequested > kCloseTimeout)
        TerminateProcess(g_Process.hProcess, 1);
}

bool EditorPlayWindow::Focus()
{
    if (!IsRunning()) return false;
    const auto windows = ProcessWindows(g_Process.dwProcessId);
    if (windows.empty()) return false;
    if (IsIconic(windows.front())) ShowWindow(windows.front(), SW_RESTORE);
    return SetForegroundWindow(windows.front()) != FALSE;
}

std::wstring EditorPlayWindow::WindowTitle()
{
    if (!IsRunning()) return {};
    for (HWND window : ProcessWindows(g_Process.dwProcessId))
    {
        if (!IsWindowVisible(window)) continue;
        wchar_t text[512]{};
        GetWindowTextW(window, text, 512);
        return text;
    }
    return {};
}

void EditorPlayWindow::Shutdown()
{
    if (!IsRunning()) return;
    for (HWND window : ProcessWindows(g_Process.dwProcessId)) PostMessageW(window, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(g_Process.hProcess, 2000) != WAIT_OBJECT_0) TerminateProcess(g_Process.hProcess, 1);
    CloseHandles();
}
