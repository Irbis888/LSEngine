#include "EditorContext.h"

#include <imgui.h>
#include <Commons.h>
#include <PhysicsCommons.h>
#include <vector>
#include <unordered_set>

namespace
{
    struct FpsAverager
    {
        std::vector<float> samples;
        float totalSeconds = 0.0f;
        float windowSeconds = 1.0f;

        void AddSample(float dt)
        {
            if (dt <= 0.0f)
                return;
            samples.push_back(dt);
            totalSeconds += dt;
            while (totalSeconds > windowSeconds && samples.size() > 1)
            {
                totalSeconds -= samples.front();
                samples.erase(samples.begin());
            }
        }

        float AverageFps() const
        {
            if (totalSeconds <= 0.0f || samples.empty())
                return 0.0f;
            return static_cast<float>(samples.size()) / totalSeconds;
        }
    };

    FpsAverager g_FpsAverager;
}

static void DrawStatisticsPanel(EditorContext& ctx, const FrameContext& frame)
{
    if (!ImGui::Begin("Statistics — Performance", &ctx.showStatistics))
    {
        ImGui::End();
        return;
    }

    const float dt = frame.timer.DeltaTime();
    g_FpsAverager.AddSample(dt);

    const float instantFps = dt > 0.0f ? 1.0f / dt : 0.0f;
    ImGui::Text("FPS (instant): %.1f", instantFps);
    ImGui::Text("FPS (1s avg): %.1f", g_FpsAverager.AverageFps());
    ImGui::Text("Frame time: %.3f ms", dt * 1000.0f);
    ImGui::Text("Total time: %.2f s", frame.timer.TotalTime());

    if (ctx.registry)
    {
        size_t entityCount = 0;
        std::unordered_set<entt::entity> seen;
        auto countView = [&](auto view)
        {
            for (auto entity : view)
            {
                if (ctx.registry->valid(entity))
                    seen.insert(entity);
            }
        };
        countView(ctx.registry->view<TagComponent>());
        countView(ctx.registry->view<TransformComponent>());
        countView(ctx.registry->view<MeshComponent>());
        countView(ctx.registry->view<CameraComponent>());
        countView(ctx.registry->view<DirectionalLightComponent>());
        countView(ctx.registry->view<PointLightComponent>());
        countView(ctx.registry->view<SpotLightComponent>());
        entityCount = seen.size();

        size_t meshCount = 0;
        for (auto entity : ctx.registry->view<MeshComponent>())
            (void)entity, ++meshCount;

        ImGui::Separator();
        ImGui::Text("Entities: %zu", entityCount);
        ImGui::Text("Mesh renderers: %zu", meshCount);
    }

    ImGui::Text("Collisions (this frame): %d", PhysicsStats::GetFrameCollisionCount());
    ImGui::TextDisabled("GPU memory: [PLACEHOLDER]");

    ImGui::End();
}

void Editor_DrawStatistics(EditorContext& ctx, const FrameContext& context)
{
    DrawStatisticsPanel(ctx, context);
}
