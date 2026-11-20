// Material instances: parent chains, shared bases, override resolution, live propagation and diagnostics.
#include "AssetPaths.h"
#include "ResourceManager.h"
#include "SceneSerializer.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

// Editor hooks referenced by engine systems linked into this headless test.
namespace ImGuiBridge
{
    bool WantsCaptureInput()
    {
        return false;
    }

    bool IsCameraRotationAllowed()
    {
        return false;
    }
}

bool Editor_IsPhysicsEnabled()
{
    return false;
}

namespace
{
    int failures = 0;

    void Check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << std::endl;
            ++failures;
        }
    }

    void WriteText(const fs::path& file, const std::string& text)
    {
        fs::create_directories(file.parent_path());
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream << text;
        if (!stream)
        {
            throw std::runtime_error("Cannot write fixture " + file.string());
        }
    }

    // Runs the action and returns its error text, or an empty string when nothing was thrown.
    template <typename Action>
    std::string ErrorOf(Action&& action)
    {
        try
        {
            action();
        }
        catch (const std::exception& error)
        {
            return error.what();
        }
        return {};
    }

    bool Contains(const std::string& text, const std::string& fragment)
    {
        return text.find(fragment) != std::string::npos;
    }

    void WriteFixtures(const fs::path& root)
    {
        WriteText(root / "Materials/Base.lsmaterial", R"({
  "version": 1,
  "material": {
    "name": "Base",
    "color": [0.5, 0.5, 0.5],
    "roughness": 0.6,
    "normal": "Textures/MissingNormal.png",
    "surface": { "mode": "masked", "alphaCutoff": 0.4, "uvScale": [2, 2], "emissiveLightRange": 0 }
  }
})");
        WriteText(root / "Materials/Child.lsmaterial", R"({
  "version": 1,
  "material": {
    "name": "Child",
    "parent": "Materials/Base.lsmaterial",
    "overrides": { "color": [0.9, 0.1, 0.1] }
  }
})");
        WriteText(root / "Materials/GrandChild.lsmaterial", R"({
  "version": 1,
  "material": {
    "name": "GrandChild",
    "parent": "Materials/Child.lsmaterial",
    "overrides": { "roughness": 0.1, "metallic": 1, "normal": "", "albedo": "Textures/MissingAlbedo.png" }
  }
})");
        WriteText(root / "Materials/BrokenParent.lsmaterial", R"({
  "version": 1,
  "material": { "parent": "Materials/DoesNotExist.lsmaterial" }
})");
        WriteText(root / "Materials/CycleA.lsmaterial", R"({
  "version": 1,
  "material": { "parent": "Materials/CycleB.lsmaterial" }
})");
        WriteText(root / "Materials/CycleB.lsmaterial", R"({
  "version": 1,
  "material": { "parent": "Materials/CycleA.lsmaterial" }
})");
        WriteText(root / "Materials/UnknownOverride.lsmaterial", R"({
  "version": 1,
  "material": { "parent": "Materials/Base.lsmaterial", "overrides": { "shininess": 3 } }
})");
        WriteText(root / "Materials/BaseOnlyOverride.lsmaterial", R"({
  "version": 1,
  "material": { "parent": "Materials/Base.lsmaterial", "overrides": { "shader": "Shaders/Unlit.hlsl" } }
})");
        WriteText(root / "Materials/InstanceWithSurface.lsmaterial", R"({
  "version": 1,
  "material": { "parent": "Materials/Base.lsmaterial", "surface": { "twoSided": true } }
})");
        WriteText(root / "Materials/WrongType.lsmaterial", R"({
  "version": 1,
  "material": { "parent": "Materials/Base.lsmaterial", "overrides": { "roughness": "high" } }
})");
    }

    void TestChainResolution(ResourceManager& resources)
    {
        // Loading the leaf loads the whole chain.
        const MaterialID grandChild = resources.LoadMaterialAsset("Materials/GrandChild.lsmaterial");
        const MaterialID child = resources.LoadMaterialAsset("Materials/Child.lsmaterial");
        const MaterialID base = resources.LoadMaterialAsset("Materials/Base.lsmaterial");
        Check(base != child && child != grandChild && base != grandChild, "Chain members must be distinct materials");
        Check(resources.GetMaterialParent(grandChild) == child && resources.GetMaterialParent(child) == base &&
            resources.GetMaterialParent(base) == 0, "Parent links do not follow the files");
        Check(resources.GetMaterialInstanceCount() == 2, "Expected exactly two registered instances");

        const Material& baseMaterial = resources.GetMaterial(base);
        const Material& childMaterial = resources.GetMaterial(child);
        const Material& grandChildMaterial = resources.GetMaterial(grandChild);
        Check(baseMaterial.base && childMaterial.base.get() == baseMaterial.base.get() &&
            grandChildMaterial.base.get() == baseMaterial.base.get(),
            "Instances must share the base MaterialBase, so they share its PSO key");
        Check(grandChildMaterial.base->mode == SurfaceMode::Masked, "Instance lost the inherited surface mode");

        const MaterialParameters& resolved = grandChildMaterial.parameters;
        Check(resolved.baseColor == glm::vec4(0.9f, 0.1f, 0.1f, 1.0f), "Color must come from the middle of the chain");
        Check(resolved.roughness == 0.1f && resolved.metallic == 1.0f, "Leaf overrides were not applied");
        Check(resolved.uvScale == glm::vec2(2.0f) && resolved.alphaCutoff == 0.4f, "Unset fields must inherit from the base");
        Check(resolved.normal == 0 && childMaterial.parameters.normal != 0, "An empty map override must clear only that instance");
        Check(resolved.albedo != 0 && resources.GetTexture(resolved.albedo).state == ResourceState::Failed,
            "A missing override map must stay visible as a Failed texture");
        Check(childMaterial.parameters.roughness == 0.6f, "Overrides leaked into the parent");
    }

    void TestLivePropagation(ResourceManager& resources)
    {
        const MaterialID base = resources.LoadMaterialAsset("Materials/Base.lsmaterial");
        const MaterialID child = resources.LoadMaterialAsset("Materials/Child.lsmaterial");
        const MaterialID grandChild = resources.LoadMaterialAsset("Materials/GrandChild.lsmaterial");

        MaterialDesc baseDescription = SceneSerializer::ReadMaterialAsset(resources.ResolveAssetPath("Materials/Base.lsmaterial"));
        baseDescription.color = glm::vec3(0.2f, 0.3f, 0.4f);
        baseDescription.surface->defaults.uvScale = glm::vec2(3.0f);
        baseDescription.surface->renderState = MaterialRenderState::AlphaBlend();
        resources.SaveMaterialAsset("Materials/Base.lsmaterial", baseDescription);

        const Material& baseMaterial = resources.GetMaterial(base);
        const Material& grandChildMaterial = resources.GetMaterial(grandChild);
        Check(resources.LoadMaterialAsset("Materials/Base.lsmaterial") == base, "Saving the base replaced its MaterialID");
        Check(resources.GetMaterial(child).base.get() == baseMaterial.base.get() &&
            grandChildMaterial.base.get() == baseMaterial.base.get(), "Instances must follow the base's new MaterialBase");
        Check(grandChildMaterial.base->renderState.blendEnabled, "Base render state did not reach the instances");
        Check(grandChildMaterial.parameters.uvScale == glm::vec2(3.0f), "Inherited parameter did not update live");
        Check(grandChildMaterial.parameters.baseColor == glm::vec4(0.9f, 0.1f, 0.1f, 1.0f),
            "A base change must not overwrite an instance's override");

        MaterialDesc childDescription = SceneSerializer::ReadMaterialAsset(resources.ResolveAssetPath("Materials/Child.lsmaterial"));
        childDescription.overrides.roughness = 0.9f;
        resources.SaveMaterialAsset("Materials/Child.lsmaterial", childDescription);
        Check(resources.GetMaterial(child).parameters.roughness == 0.9f, "Instance save did not update its material");
        Check(grandChildMaterial.parameters.roughness == 0.1f, "A middle change must not overwrite the leaf's override");
        Check(grandChildMaterial.parameters.baseColor == glm::vec4(0.9f, 0.1f, 0.1f, 1.0f), "Leaf lost its inherited color");

        const MaterialDesc restored = SceneSerializer::ReadMaterialAsset(resources.ResolveAssetPath("Materials/Child.lsmaterial"));
        Check(restored.IsInstance() && restored.parent == "Materials/Base.lsmaterial" &&
            restored.overrides == childDescription.overrides && !restored.surface, "Instance file round trip changed its content");

        // The editor preview and inline materials resolve parents the same way as files.
        MaterialDesc preview;
        preview.name = "Preview";
        preview.parent = "Materials/GrandChild.lsmaterial";
        preview.overrides.emissiveIntensity = 4.0f;
        const MaterialID previewMaterial = resources.CreateTexturedMaterial(preview);
        Check(resources.GetMaterial(previewMaterial).base.get() == baseMaterial.base.get() &&
            resources.GetMaterial(previewMaterial).parameters.roughness == 0.1f &&
            resources.GetMaterial(previewMaterial).parameters.emissiveIntensity == 4.0f, "In-memory instance resolved incorrectly");
    }

    void TestDiagnostics(ResourceManager& resources)
    {
        const MaterialID child = resources.LoadMaterialAsset("Materials/Child.lsmaterial");
        const size_t instancesBefore = resources.GetMaterialInstanceCount();

        const std::string missingParent = ErrorOf([&]
        {
            resources.LoadMaterialAsset("Materials/BrokenParent.lsmaterial");
        });
        Check(Contains(missingParent, "BrokenParent.lsmaterial") && Contains(missingParent, "DoesNotExist.lsmaterial"),
            "Missing parent diagnostic must name the instance and the parent: " + missingParent);

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const std::string cycle = ErrorOf([&]
            {
                resources.LoadMaterialAsset("Materials/CycleA.lsmaterial");
            });
            Check(Contains(cycle, "inheritance cycle") && Contains(cycle, "CycleA.lsmaterial -> CycleB.lsmaterial -> CycleA.lsmaterial"),
                "Cycle diagnostic must show the chain: " + cycle);
        }

        const std::string unknown = ErrorOf([&]
        {
            resources.LoadMaterialAsset("Materials/UnknownOverride.lsmaterial");
        });
        Check(Contains(unknown, "UnknownOverride.lsmaterial") && Contains(unknown, "overrides.shininess"),
            "Unknown override diagnostic: " + unknown);
        const std::string baseOnly = ErrorOf([&]
        {
            resources.LoadMaterialAsset("Materials/BaseOnlyOverride.lsmaterial");
        });
        Check(Contains(baseOnly, "overrides.shader") && Contains(baseOnly, "base material"), "Base-only override diagnostic: " + baseOnly);
        const std::string withSurface = ErrorOf([&]
        {
            resources.LoadMaterialAsset("Materials/InstanceWithSurface.lsmaterial");
        });
        Check(Contains(withSurface, "surface") && Contains(withSurface, "overrides"), "Instance surface diagnostic: " + withSurface);
        const std::string wrongType = ErrorOf([&]
        {
            resources.LoadMaterialAsset("Materials/WrongType.lsmaterial");
        });
        Check(Contains(wrongType, "overrides.roughness") && Contains(wrongType, "number"), "Wrong type diagnostic: " + wrongType);

        // Saving must refuse cycles before the file is touched.
        const fs::path baseFile = resources.ResolveAssetPath("Materials/Base.lsmaterial");
        MaterialDesc cyclicBase;
        cyclicBase.name = "Base";
        cyclicBase.parent = "Materials/GrandChild.lsmaterial";
        const std::string saveCycle = ErrorOf([&]
        {
            resources.SaveMaterialAsset("Materials/Base.lsmaterial", cyclicBase);
        });
        Check(Contains(saveCycle, "inheritance cycle"), "Saving a cyclic parent was accepted: " + saveCycle);
        Check(!SceneSerializer::ReadMaterialAsset(baseFile).IsInstance(), "A rejected save modified the file");

        MaterialDesc selfParent;
        selfParent.parent = "Materials/Child.lsmaterial";
        const std::string saveSelf = ErrorOf([&]
        {
            resources.SaveMaterialAsset("Materials/Child.lsmaterial", selfParent);
        });
        Check(Contains(saveSelf, "inheritance cycle"), "Self inheritance was accepted: " + saveSelf);

        MaterialDesc uncachedCycle;
        uncachedCycle.parent = "Materials/NewLeaf.lsmaterial";
        WriteText(resources.ResolveAssetPath("Materials/NewLeaf.lsmaterial"),
            R"({"version": 1, "material": {"parent": "Materials/NewRoot.lsmaterial"}})");
        const std::string saveUncached = ErrorOf([&]
        {
            resources.SaveMaterialAsset("Materials/NewRoot.lsmaterial", uncachedCycle);
        });
        Check(Contains(saveUncached, "inheritance cycle") && !fs::exists(resources.ResolveAssetPath("Materials/NewRoot.lsmaterial")),
            "A cycle through an unloaded file was written: " + saveUncached);

        const std::string renderState = ErrorOf([&]
        {
            resources.SetMaterialRenderState(child, MaterialRenderState::Additive());
        });
        Check(Contains(renderState, "instance"), "Instance accepted a render state of its own: " + renderState);
        Check(resources.GetMaterialInstanceCount() == instancesBefore, "Failed loads registered instances");
    }
}

namespace
{
    // Writes the file and moves its time forward, so the change is visible regardless of timestamp resolution.
    void EditOnDisk(ResourceManager& resources, const std::string& path, const std::string& text)
    {
        static int edit = 0;
        const fs::path file = resources.ResolveAssetPath(path);
        WriteText(file, text);
        fs::last_write_time(file, fs::file_time_type::clock::now() + std::chrono::seconds(++edit));
    }

    bool Contains(const std::vector<std::string>& lines, const std::string& fragment)
    {
        for (const auto& line : lines)
        {
            if (line.find(fragment) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    void TestHotReload(ResourceManager& resources)
    {
        const MaterialID base = resources.LoadMaterialAsset("Materials/Base.lsmaterial");
        const MaterialID child = resources.LoadMaterialAsset("Materials/Child.lsmaterial");
        const MaterialID grandChild = resources.LoadMaterialAsset("Materials/GrandChild.lsmaterial");

        auto idle = resources.ReloadChangedMaterialAssets();
        Check(idle.reloaded.empty() && idle.errors.empty(), "Reload without disk changes touched materials");

        // External edit of the base: users and every instance update in place, overrides survive.
        EditOnDisk(resources, "Materials/Base.lsmaterial", R"({
  "version": 1,
  "material": {
    "name": "Base",
    "color": [0.1, 0.6, 0.2],
    "roughness": 0.7,
    "surface": { "mode": "masked", "alphaCutoff": 0.25, "uvScale": [5, 5], "emissiveLightRange": 0 }
  }
})");
        auto edited = resources.ReloadChangedMaterialAssets();
        Check(edited.errors.empty() && Contains(edited.reloaded, "Base.lsmaterial") && edited.reloaded.size() == 1,
            "Base edit was not reloaded exactly once");
        Check(resources.LoadMaterialAsset("Materials/Base.lsmaterial") == base, "Hot reload replaced the MaterialID");
        Check(resources.GetMaterial(base).parameters.baseColor == glm::vec4(0.1f, 0.6f, 0.2f, 1.0f),
            "Base parameters did not reload");
        Check(resources.GetMaterial(grandChild).parameters.uvScale == glm::vec2(5.0f) &&
            resources.GetMaterial(grandChild).parameters.alphaCutoff == 0.25f, "Instances did not inherit the reloaded base");
        Check(resources.GetMaterial(grandChild).parameters.baseColor == glm::vec4(0.9f, 0.1f, 0.1f, 1.0f) &&
            resources.GetMaterial(grandChild).parameters.roughness == 0.1f, "Reload overwrote instance overrides");
        Check(resources.GetMaterial(grandChild).base.get() == resources.GetMaterial(base).base.get(),
            "Instances lost the shared base after reload");

        // A broken edit keeps the running version and reports the file.
        const MaterialParameters childBefore = resources.GetMaterial(child).parameters;
        EditOnDisk(resources, "Materials/Child.lsmaterial", R"({ "version": 1, "material": { "parent": )");
        auto broken = resources.ReloadChangedMaterialAssets();
        Check(Contains(broken.errors, "Child.lsmaterial") && broken.reloaded.empty(), "Broken edit was not reported");
        Check(resources.GetMaterial(child).parameters.roughness == childBefore.roughness &&
            resources.GetMaterial(child).parameters.baseColor == childBefore.baseColor, "Broken edit replaced the running material");
        Check(resources.GetMaterialAssetError("Materials/Child.lsmaterial").find("Child.lsmaterial") != std::string::npos,
            "Broken file has no stored error");
        auto repeated = resources.ReloadChangedMaterialAssets();
        Check(repeated.errors.empty() && repeated.reloaded.empty(), "An unchanged broken file was retried every pass");

        // Fixing it applies the new overrides and clears the error.
        EditOnDisk(resources, "Materials/Child.lsmaterial", R"({
  "version": 1,
  "material": { "name": "Child", "parent": "Materials/Base.lsmaterial", "overrides": { "color": [0.2, 0.2, 0.9] } }
})");
        auto fixed = resources.ReloadChangedMaterialAssets();
        Check(fixed.errors.empty() && Contains(fixed.reloaded, "Child.lsmaterial"), "Fixed file was not reloaded");
        Check(resources.GetMaterialAssetError("Materials/Child.lsmaterial").empty(), "Fixed file kept its error");
        Check(resources.GetMaterial(child).parameters.baseColor == glm::vec4(0.2f, 0.2f, 0.9f, 1.0f) &&
            resources.GetMaterial(child).parameters.roughness == 0.7f, "Fixed instance resolved incorrectly");
        Check(resources.GetMaterial(grandChild).parameters.baseColor == glm::vec4(0.2f, 0.2f, 0.9f, 1.0f),
            "Grandchild did not follow the reloaded middle of the chain");

        // An edit that closes a cycle is rejected; the running chain stays.
        EditOnDisk(resources, "Materials/Base.lsmaterial", R"({
  "version": 1,
  "material": { "name": "Base", "parent": "Materials/GrandChild.lsmaterial" }
})");
        auto cycle = resources.ReloadChangedMaterialAssets();
        Check(Contains(cycle.errors, "inheritance cycle") && resources.GetMaterialParent(base) == 0,
            "Cyclic hot reload was accepted");

        // The engine's own save is not an external edit.
        MaterialDesc restoredBase;
        restoredBase.name = "Base";
        restoredBase.color = glm::vec3(0.5f);
        restoredBase.surface.emplace();
        resources.SaveMaterialAsset("Materials/Base.lsmaterial", restoredBase);
        auto afterSave = resources.ReloadChangedMaterialAssets();
        Check(afterSave.reloaded.empty() && afterSave.errors.empty(), "Own save was reloaded again");
        Check(resources.GetMaterialAssetError("Materials/Base.lsmaterial").empty(),
            "A successful save did not clear the previous reload error");
    }
}

int main(int argc, char** argv)
{
    try
    {
        const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "LSMaterialInstanceTests";
        fs::remove_all(root);
        WriteFixtures(root);
        ResourceManager resources;
        resources.SetAssetRoot(root);
        TestChainResolution(resources);
        TestLivePropagation(resources);
        TestDiagnostics(resources);
        TestHotReload(resources);
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: unexpected exception: " << error.what() << std::endl;
        return 1;
    }
    if (failures != 0)
    {
        std::cerr << failures << " material instance check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "MATERIAL_INSTANCE_TESTS_PASS" << std::endl;
    return 0;
}
