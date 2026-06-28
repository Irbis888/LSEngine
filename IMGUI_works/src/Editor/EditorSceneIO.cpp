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

        if (!ctx.engine->LoadScene(EditorSceneIO::PlaySnapshotPath(), outError))
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

    std::string error;
    if (ctx.engine->SaveScene(DefaultSavePath(), error))
        ctx.statusMessage = std::string("Saved to ") + DefaultSavePath();
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
        ctx.statusMessage = std::string("Loaded from ") + path;
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
    if (!ctx.engine->SaveScene(PlaySnapshotPath(), error))
    {
        ctx.statusMessage = "Play failed (snapshot): " + error;
        return;
    }

    ctx.mode = EditorMode::Play;
    ctx.statusMessage = std::string("Play — snapshot at ") + PlaySnapshotPath();
}

void EditorSceneIO::RestorePlaySnapshot(EditorContext& ctx)
{
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

    if (!ctx.restoreSceneOnStop)
    {
        ctx.statusMessage = "Stopped — scene unchanged (Debug: Restore on Stop is off).";
        return;
    }

    std::string error;
    if (RestorePlaySnapshotInternal(ctx, error))
        ctx.statusMessage = "Stopped — scene restored from Play snapshot.";
    else
        ctx.statusMessage = "Stop failed (restore): " + error;
}
