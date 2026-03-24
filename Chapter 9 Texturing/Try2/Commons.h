#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif


#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cassert>
#include <iostream>
#include <GameTimer.h>
#include <windows.h>
#include <glm/glm.hpp>

using MeshID = uint32_t;
using MaterialID = uint32_t;
using TextureID = uint32_t;

struct TransformComponent
{
	glm::vec3 position;
	glm::vec3 rotation;
	glm::vec3 scale;
};
struct TagComponent
{
	std::string tag;
};
struct MeshComponent
{
	MeshID meshID;
};

class Commons{};

