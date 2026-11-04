#include "TerrainPanel.h"
#include <TerrainSystem.h>
#include <Engine.h>
#include <AssetPaths.h>
#include <imgui.h>
#include <commdlg.h>
#pragma comment(lib,"Comdlg32.lib")

static void PathField(const char* label,std::string& path,EditorContext& ctx)
{
    ImGui::PushID(label);char text[4097]{};strncpy_s(text,path.c_str(),sizeof(text)-1);
    if(ImGui::InputText(label,text,sizeof(text)))path=text;
    ImGui::SameLine();
    if(ImGui::Button("Browse"))
    {
        wchar_t file[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.lpstrFile=file;dialog.nMaxFile=32768;
        dialog.lpstrFilter=L"Image files\0*.png;*.dds;*.tga;*.bmp;*.pgm;*.jpg\0All files\0*.*\0";
        auto root=ctx.engine->GetAssetRoot().wstring();dialog.lpstrInitialDir=root.c_str();dialog.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
        if(GetOpenFileNameW(&dialog))path=AssetPaths::ToUtf8(std::filesystem::path(file).lexically_relative(ctx.engine->GetAssetRoot()));
    }
    ImGui::PopID();
}
void Editor_DrawTerrainInspector(EditorContext& ctx,entt::entity entity)
{
    if(!ctx.engine)return;auto& reg=*ctx.registry;
    auto* s=reg.try_get<TerrainComponent>(entity);
    if(!s){if(ImGui::Button("Add Terrain"))reg.emplace<TerrainComponent>(entity);return;}
    if(!ImGui::CollapsingHeader("Terrain",ImGuiTreeNodeFlags_DefaultOpen))return;
    auto& system=ctx.engine->GetTerrain();ImGui::TextWrapped("%s",system.GetStatus(entity).c_str());
    auto stats=system.GetStats(entity);
    ImGui::Text("Chunks: %zu visible / %zu culled",stats.visible,stats.culled);
    ImGui::Text("Triangles: %zu | draws: %zu",stats.triangles,stats.draws);
    ImGui::Text("Build %.2f ms | selection %.3f ms",stats.buildMs,stats.selectMs);
    ImGui::Text("CPU %.2f MiB | all terrain GPU %.2f MiB",stats.cpuBytes/1048576.,stats.gpuBytes/1048576.);
    ImGui::TextWrapped("Translation and positive scale only. Rotation is unsupported. Source/material edits apply with Rebuild; the previous ready version stays visible.");
    const char* sources[]={"procedural","heightmap","flat"};int source=s->source=="heightmap"?1:s->source=="flat"?2:0;
    if(ImGui::Combo("Height source",&source,"Procedural\0Heightmap\0Flat\0"))s->source=sources[source];
    if(source==1){PathField("Heightmap",s->heightmap,ctx);ImGui::TextWrapped("Single-channel 8/16-bit image or linear R8/R16/R32 DDS; dimensions must match samples. Heights are linear [0,1].");}
    ImGui::InputScalar("Samples X",ImGuiDataType_U32,&s->samplesX);ImGui::InputScalar("Samples Z",ImGuiDataType_U32,&s->samplesZ);
    ImGui::TextDisabled("N cells require N+1 samples.");
    ImGui::InputFloat("Size X",&s->sizeX);ImGui::InputFloat("Size Z",&s->sizeZ);
    ImGui::InputFloat("Height scale",&s->heightScale);ImGui::InputFloat("Height offset",&s->heightOffset);
    ImGui::InputScalar("Chunk cells",ImGuiDataType_U32,&s->chunkCells);
    if(source==0){ImGui::InputScalar("Seed",ImGuiDataType_U32,&s->seed);ImGui::InputFloat("Noise frequency",&s->frequency,0,0,"%.5f");}
    ImGui::InputScalar("LOD levels",ImGuiDataType_U32,&s->lodCount);ImGui::InputFloat("First LOD distance",&s->lodDistance);
    ImGui::SliderFloat("LOD hysteresis",&s->hysteresis,0,.45f);ImGui::InputFloat("Minimum skirt depth",&s->skirtDepth);
    if(ImGui::TreeNode("Material layers"))
    {
        int count=int(s->layerCount);if(ImGui::SliderInt("Layers",&count,1,4))s->layerCount=uint32_t(count);
        PathField("Weightmap (optional)",s->weightmap,ctx);
        ImGui::TextWrapped("Empty weightmap: weights generated from height/slope. RGBA8 weightmap uses whole-map UV; material textures repeat by tile size.");
        for(uint32_t i=0;i<std::min(s->layerCount,4u);++i)
        {
            ImGui::PushID(int(i));ImGui::SeparatorText(("Layer "+std::to_string(i)).c_str());auto& l=s->layers[i];
            PathField("Albedo",l.albedo,ctx);PathField("Normal map",l.normal,ctx);ImGui::ColorEdit3("Tint",&l.color.x);
            ImGui::SliderFloat("Roughness",&l.roughness,0,1);ImGui::InputFloat("Tile size (world units)",&l.tileSize);ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if(ImGui::Button("Apply / Rebuild Terrain"))++s->revision;
    ImGui::Checkbox("Frustum culling",&s->culling);ImGui::Checkbox("Wireframe",&s->wireframe);
    ImGui::Checkbox("LOD colors",&s->showLod);ImGui::Checkbox("Chunk AABBs",&s->showBounds);
    ImGui::Checkbox("Heightfield collision",&s->collision);ImGui::SliderFloat("Terrain friction",&s->friction,0,1);ImGui::SliderFloat("Terrain restitution",&s->restitution,0,1);
    if(ImGui::Button("Remove Terrain"))reg.remove<TerrainComponent>(entity);
}
