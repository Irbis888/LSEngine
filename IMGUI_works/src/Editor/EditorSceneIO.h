#pragma once

#include <string>

struct EditorContext;

namespace EditorSceneIO
{
    inline const char* DefaultSavePath() { return "Scenes/EditorSave.json"; }
    inline const char* PlaySnapshotPath() { return "Scenes/EditorPlaySnapshot.json"; }
    inline const char* DemoScenePath() { return "Scenes/DemoScene.json"; }
    inline const char* CharacterShowcasePath() { return "Scenes/CharacterShowcase.json"; }

    void SaveScene(EditorContext& ctx);
    void LoadScene(EditorContext& ctx);
    void LoadScene(EditorContext& ctx, const char* path);
    void BeginPlay(EditorContext& ctx);
    void EndPlay(EditorContext& ctx);
    void RestorePlaySnapshot(EditorContext& ctx);
}
