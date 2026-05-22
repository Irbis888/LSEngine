#pragma once

#include <string>

struct EditorContext;

namespace EditorSceneIO
{
    inline const char* DefaultSavePath() { return "Scenes/EditorSave.json"; }
    inline const char* PlaySnapshotPath() { return "Scenes/EditorPlaySnapshot.json"; }

    void SaveScene(EditorContext& ctx);
    void LoadScene(EditorContext& ctx);
    void BeginPlay(EditorContext& ctx);
    void EndPlay(EditorContext& ctx);
    void RestorePlaySnapshot(EditorContext& ctx);
}
