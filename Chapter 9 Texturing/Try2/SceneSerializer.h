#pragma once

#include <string>

#include "ResourceManager.h"
#include "World.h"

class SceneSerializer
{
public:
    static void Save(const World& world, const std::string& path);
    static void Load(World& world, const std::string& path);
    static void Load(World& world, ResourceManager& resources, const std::string& path);
};
