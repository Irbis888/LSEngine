#pragma once

#include <entt/entt.hpp>
#include <string>

struct EditorContext;

void Editor_DrawMaterialWindow(EditorContext& context);
void Editor_DrawMaterialComponent(EditorContext& context, entt::entity entity);
void Editor_OpenMaterial(EditorContext& context, const std::string& path);
