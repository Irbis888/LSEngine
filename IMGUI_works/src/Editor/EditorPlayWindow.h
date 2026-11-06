#pragma once

#include <string>

struct EditorContext;

// Play mode in a separate window: the editor writes the current (edited, unsaved) scene to
// <project>/Temp/Play/<Scene>.json and starts "Try2.exe --play --project <file> --scene ...".
// The game runs in that process with its own window, swap chain, input, camera, physics and
// Lua state; the editor's registry is never touched, so Stop (or closing the game window)
// leaves the editor exactly in the state it had before Play.
namespace EditorPlayWindow
{
    bool Start(EditorContext& ctx);
    // Asks the game window to close (WM_CLOSE); terminates it if it does not exit in time.
    void Stop(EditorContext& ctx);
    // Call once per editor frame: notices a closed game window.
    void Update(EditorContext& ctx);
    bool IsRunning();
    unsigned long ProcessId();
    // Brings the game window to the front. False when it has no window yet.
    bool Focus();
    // Title of the game window (empty before it exists).
    std::wstring WindowTitle();
    // Editor exit: the game process is closed with the editor.
    void Shutdown();
    // Project-relative path of the scene file written for the last Play.
    const std::string& SessionScene();
}
