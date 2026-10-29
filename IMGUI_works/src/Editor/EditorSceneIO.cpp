#include "EditorSceneIO.h"
#include "EditorContext.h"

#include <Engine.h>
#include <entt/entt.hpp>
#include <string>

namespace
{
    bool RestorePlaySnapshotInternal(EditorContext& ctx, std::string& outError)
    {
        if (!ctx.engine)
        {
            outError = "engine not bound";
            return false;
        }

        if (!ctx.engine->RestorePlaySnapshot(EditorSceneIO::PlaySnapshotPath(), outError))
            return false;

        ctx.selected = entt::null;
        ctx.registry = &ctx.engine->GetRegistry();
        return true;
    }
}

void EditorSceneIO::SaveScene(EditorContext& ctx)
{
    if (!ctx.engine)
    {
        ctx.statusMessage = "Save failed: engine not bound.";
        return;
    }

    SaveScene(ctx, DefaultSavePath());
}

void EditorSceneIO::SaveCurrentScene(EditorContext& ctx)
{
    if (!ctx.engine || ctx.engine->GetScenePath().empty())
    {
        ctx.statusMessage = "Save failed: no scene file is loaded.";
        return;
    }
    SaveScene(ctx, ctx.engine->GetScenePath().c_str());
}

void EditorSceneIO::SaveScene(EditorContext& ctx, const char* path)
{
    if (!ctx.engine)
    {
        ctx.statusMessage = "Save failed: engine not bound.";
        return;
    }
    if (ctx.mode != EditorMode::Edit)
    {
        ctx.statusMessage = "Save failed: stop Play first (Play changes are not scene data).";
        return;
    }

    std::string error;
    size_t runtimeOnly = 0;
    if (ctx.engine->SaveScene(path, error, false, &runtimeOnly))
    {
        ctx.statusMessage = std::string("Saved to ") + path;
        if (runtimeOnly)
            ctx.statusMessage += " (" + std::to_string(runtimeOnly) +
                " meshes have no source description and are valid only until restart)";
    }
    else
        ctx.statusMessage = "Save failed: " + error;
}

void EditorSceneIO::LoadScene(EditorContext& ctx)
{
    LoadScene(ctx, DefaultSavePath());
}

void EditorSceneIO::LoadScene(EditorContext& ctx, const char* path)
{
    if (!ctx.engine)
    {
        ctx.statusMessage = "Load failed: engine not bound.";
        return;
    }

    std::string error;
    if (ctx.engine->LoadScene(path, error))
    {
        ctx.selected = entt::null;
        ctx.registry = &ctx.engine->GetRegistry();
        ctx.statusMessage = std::string("Loading ") + path;
    }
    else
        ctx.statusMessage = "Load failed: " + error;
}

void EditorSceneIO::BeginPlay(EditorContext& ctx)
{
    if (!ctx.engine)
    {
        ctx.statusMessage = "Play failed: engine not bound.";
        return;
    }

    std::string error;
    // The snapshot is restored in this session, so it may reuse runtime mesh ids.
    if (!ctx.engine->SaveScene(PlaySnapshotPath(), error, true))
    {
        ctx.statusMessage = "Play failed (snapshot): " + error;
        return;
    }

    if (!ctx.engine->BeginPlay(error))
    {
        ctx.statusMessage = "Play failed (scripts): " + error;
        return;
    }
    ctx.mode = EditorMode::Play;
    ctx.statusMessage = std::string("Play — snapshot at ") + PlaySnapshotPath();
}

void EditorSceneIO::RestorePlaySnapshot(EditorContext& ctx)
{
    ctx.mode = EditorMode::Edit;
    std::string error;
    if (RestorePlaySnapshotInternal(ctx, error))
        ctx.statusMessage = std::string("Restored from ") + PlaySnapshotPath();
    else
        ctx.statusMessage = "Restore snapshot failed: " + error;
}

void EditorSceneIO::EndPlay(EditorContext& ctx)
{
    if (!ctx.engine)
    {
        ctx.statusMessage = "Stop failed: engine not bound.";
        ctx.mode = EditorMode::Edit;
        return;
    }

    ctx.mode = EditorMode::Edit;
    ctx.engine->EndPlay();

    std::string error;
    if (RestorePlaySnapshotInternal(ctx, error))
        ctx.statusMessage = "Stopped — scene restored from Play snapshot.";
    else
        ctx.statusMessage = "Stop failed (restore): " + error;
}
