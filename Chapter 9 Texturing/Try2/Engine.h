#pragma once

#include "Commons.h"
#include "World.h"
#include "ResourceManager.h"

class Engine
{
private:
	World world;

	std::vector<std::unique_ptr<ISystem>> updateSystems;
	std::vector<std::unique_ptr<ISystem>> physicsSystems;
	std::vector<std::unique_ptr<ISystem>> renderSystems;

    IRenderAdapter* mRenderAdapter;
	ResourceManager mResourceManager;
public:
    Engine(IRenderAdapter* renderer)
        : mRenderAdapter(renderer) {
    };

	void Init(const GameTimer& gt);
	void Update(const FrameContext& context);
	void PhysicsUpdate(const FrameContext& context);
	void Draw(const FrameContext& context);

	World& GetWorld() { return world; }
	entt::registry& GetRegistry() { return world.registry; }
	ResourceManager& GetResources() { return mResourceManager; }

	bool SaveScene(const std::string& path, std::string& outError);
	bool LoadScene(const std::string& path, std::string& outError);
};
