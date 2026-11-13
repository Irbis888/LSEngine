#include "D3DRenderAdapter.h"
#include "AssetPaths.h"
#include <d3d12shader.h>
#include <filesystem>
#include <cstring>

namespace
{
    void CheckMaterialLayout(ID3D12ShaderReflectionConstantBuffer& buffer)
    {
        struct Field
        {
            const char* name;
            UINT offset;
            UINT size;
        };
        const Field fields[] = {
            {"BaseColorFactor", offsetof(MaterialConstants, baseColor), 16},
            {"EmissiveColor", offsetof(MaterialConstants, emissiveColor), 12},
            {"EmissiveIntensity", offsetof(MaterialConstants, emissiveIntensity), 4},
            {"UVScale", offsetof(MaterialConstants, uvScale), 8},
            {"UVOffset", offsetof(MaterialConstants, uvOffset), 8},
            {"RoughnessFactor", offsetof(MaterialConstants, roughness), 4},
            {"MetallicFactor", offsetof(MaterialConstants, metallic), 4},
            {"NormalStrength", offsetof(MaterialConstants, normalStrength), 4},
            {"AOStrength", offsetof(MaterialConstants, aoStrength), 4},
            {"AlphaCutoff", offsetof(MaterialConstants, alphaCutoff), 4},
            {"TextureFlags", offsetof(MaterialConstants, textureFlags), 4}
        };
        for (const auto& field : fields)
        {
            D3D12_SHADER_VARIABLE_DESC variable{};
            if (FAILED(buffer.GetVariableByName(field.name)->GetDesc(&variable)) ||
                variable.StartOffset != field.offset || variable.Size != field.size)
            {
                throw std::runtime_error(std::string("SurfaceMaterial layout mismatch for ") + field.name);
            }
        }
    }

    void CheckSurfaceShader(ID3DBlob* bytecode, bool pixel, bool editor)
    {
        ComPtr<ID3D12ShaderReflection> reflection;
        ThrowIfFailed(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(&reflection)));
        D3D12_SHADER_DESC shader{};
        ThrowIfFailed(reflection->GetDesc(&shader));

        for (UINT index = 0; index < shader.BoundResources; ++index)
        {
            D3D12_SHADER_INPUT_BIND_DESC binding{};
            ThrowIfFailed(reflection->GetResourceBindingDesc(index, &binding));
            bool compatible = false;
            if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint < 4)
            {
                const UINT expectedSizes[] = {sizeof(ObjectConstants), sizeof(PassConstants), sizeof(MaterialConstants), 16};
                const char* expectedNames[] = {"SurfaceObject", "SurfaceFrame", "SurfaceMaterial", "SurfaceSelection"};
                D3D12_SHADER_BUFFER_DESC buffer{};
                auto* reflectedBuffer = reflection->GetConstantBufferByName(binding.Name);
                ThrowIfFailed(reflectedBuffer->GetDesc(&buffer));
                compatible = buffer.Size == expectedSizes[binding.BindPoint] &&
                    std::strcmp(binding.Name, expectedNames[binding.BindPoint]) == 0 &&
                    (binding.BindPoint != 3 || (pixel && editor));
                if (compatible && binding.BindPoint == 2)
                {
                    CheckMaterialLayout(*reflectedBuffer);
                }
            }
            else if (pixel && binding.Type == D3D_SIT_TEXTURE)
            {
                compatible = binding.BindPoint < 4 && binding.Dimension == D3D_SRV_DIMENSION_TEXTURE2D &&
                    binding.ReturnType == D3D_RETURN_TYPE_FLOAT;
            }
            else if (pixel && binding.Type == D3D_SIT_SAMPLER)
            {
                compatible = binding.BindPoint == 4;
            }

            if (!compatible || binding.Space != 0 || binding.BindCount != 1)
            {
                throw std::runtime_error(std::string("Surface shader has an incompatible binding: ") + binding.Name);
            }
        }

        if (!pixel)
        {
            for (UINT index = 0; index < shader.InputParameters; ++index)
            {
                D3D12_SIGNATURE_PARAMETER_DESC parameter{};
                ThrowIfFailed(reflection->GetInputParameterDesc(index, &parameter));
                const std::string_view semantic(parameter.SemanticName);
                const bool vector = semantic == "POSITION" || semantic == "NORMAL" || semantic == "TANGENT";
                const bool uv = semantic == "TEXCOORD";
                if ((!vector && !uv) || parameter.SemanticIndex != 0 ||
                    parameter.ComponentType != D3D_REGISTER_COMPONENT_FLOAT32 ||
                    parameter.Mask != (uv ? 3 : 7))
                {
                    throw std::runtime_error("Surface vertex shader expects POSITION/NORMAL/TANGENT float3 and TEXCOORD float2");
                }
            }
            return;
        }

        if (shader.OutputParameters != (editor ? 2u : 1u))
        {
            throw std::runtime_error("Surface pixel shader must write color and, in the editor variant, a uint entity ID");
        }
        for (UINT index = 0; index < shader.OutputParameters; ++index)
        {
            D3D12_SIGNATURE_PARAMETER_DESC parameter{};
            ThrowIfFailed(reflection->GetOutputParameterDesc(index, &parameter));
            const bool color = parameter.SemanticIndex == 0 && parameter.Mask == 15 &&
                parameter.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32;
            const bool identifier = editor && parameter.SemanticIndex == 1 && parameter.Mask == 1 &&
                parameter.ComponentType == D3D_REGISTER_COMPONENT_UINT32;
            if (parameter.SystemValueType != D3D_NAME_TARGET || (!color && !identifier))
            {
                throw std::runtime_error("Surface output must be float4 SV_Target0 and optional uint SV_Target1");
            }
        }
    }

    bool IsSrgbView(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return true;
        default:
            return false;
        }
    }
}

D3DRenderAdapter::SurfacePipelineKey D3DRenderAdapter::MakeSurfaceKey(const MaterialBase& base, bool editor) const
{
    SurfacePipelineKey key;
    key.program = base.program;
    key.program.file = std::filesystem::path(base.program.file).lexically_normal().generic_wstring();
    key.program.editorVariant = true;
    key.program.maskedVariant = true;
    key.mode = base.mode;
    key.twoSided = base.twoSided;
    key.colorFormat = mBackBufferFormat;
    key.identifierFormat = editor ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_UNKNOWN;
    key.depthFormat = mDepthStencilFormat;
    // Both the flip-model swapchain and the editor targets are single sample.
    return key;
}

ComPtr<ID3DBlob> D3DRenderAdapter::FindSurfaceShader(const SurfaceShaderKey& key)
{
    const auto found = mSurfaceShaders.find(key);
    if (found != mSurfaceShaders.end())
    {
        return found->second;
    }

    D3D_SHADER_MACRO defines[3]{};
    UINT defineCount = 0;
    if (key.editor)
    {
        defines[defineCount++] = {"EDITOR_VIEWPORT", "1"};
    }
    if (key.masked)
    {
        defines[defineCount++] = {"SURFACE_MASKED", "1"};
    }
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    auto shaderFile = std::filesystem::path(key.file);
    if (shaderFile.is_relative() && mResourceManager &&
        shaderFile != L"Shaders/Default.hlsl" && shaderFile != L"Shaders/Unlit.hlsl")
    {
        const auto projectFile = mResourceManager->GetAssetSearch().root / shaderFile;
        std::error_code error;
        if (std::filesystem::is_regular_file(projectFile, error))
        {
            shaderFile = projectFile;
        }
    }
    const HRESULT result = D3DCompileFromFile(shaderFile.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        key.entry.c_str(), key.pixel ? "ps_5_1" : "vs_5_1", flags, 0, &bytecode, &errors);
    const std::string label = AssetPaths::ToUtf8(std::filesystem::path(key.file)) + " [" + key.entry + "]";
    if (FAILED(result))
    {
        throw std::runtime_error(label + ": " + (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "HLSL compilation failed"));
    }
    try
    {
        CheckSurfaceShader(bytecode.Get(), key.pixel, key.editor);
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error(label + ": " + error.what());
    }
    mSurfaceShaders.emplace(key, bytecode);
    return bytecode;
}

ComPtr<ID3D12PipelineState> D3DRenderAdapter::BuildSurfacePipeline(const SurfacePipelineKey& key)
{
    const bool editor = key.identifierFormat != DXGI_FORMAT_UNKNOWN;
    const auto vertex = FindSurfaceShader({key.program.file, key.program.vertexEntry});
    const auto pixel = FindSurfaceShader({key.program.file, key.program.pixelEntry, true, editor, key.mode == SurfaceMode::Masked});
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.InputLayout = {mInputLayout.data(), static_cast<UINT>(mInputLayout.size())};
    description.pRootSignature = mRootSignatures.at("standard").Get();
    description.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    description.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    description.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    description.RasterizerState.CullMode = key.twoSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
    description.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    description.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    description.SampleMask = UINT_MAX;
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = editor ? 2 : 1;
    description.RTVFormats[0] = key.colorFormat;
    description.RTVFormats[1] = key.identifierFormat;
    description.DSVFormat = key.depthFormat;
    description.SampleDesc = {key.samples, key.sampleQuality};

    ComPtr<ID3D12PipelineState> pipeline;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipeline)));
    return pipeline;
}

ID3D12PipelineState* D3DRenderAdapter::FindSurfacePipeline(const SurfacePipelineKey& key)
{
    const auto found = mSurfacePipelines.find(key);
    if (found != mSurfacePipelines.end())
    {
        ++mSurfaceCacheStats.hits;
        return found->second.Get();
    }
    if (mSurfaceFailures.contains(key))
    {
        return nullptr;
    }

    ++mSurfaceCacheStats.misses;
    try
    {
        auto pipeline = BuildSurfacePipeline(key);
        auto* result = pipeline.Get();
        mSurfacePipelines.emplace(key, std::move(pipeline));
        ++mSurfaceCacheStats.created;
        return result;
    }
    catch (const std::exception& error)
    {
        mSurfaceDiagnostic = error.what();
    }
    catch (const DxException& error)
    {
        mSurfaceDiagnostic = AssetPaths::ToUtf8(std::filesystem::path(key.program.file)) +
            ": D3D12 rejected the surface pipeline (HRESULT " + std::to_string(error.ErrorCode) + ")";
    }
    mSurfaceFailures.emplace(key, mSurfaceDiagnostic);
    std::cerr << "Material shader fallback: " << mSurfaceDiagnostic << '\n';
    return nullptr;
}

int D3DRenderAdapter::SurfaceTextureDescriptor(TextureID id, TextureID fallback, bool srgb, DXGI_FORMAT& format, bool* loaded)
{
    auto found = mTextures.find(std::to_string(id));
    const bool usable = found != mTextures.end() && found->second->surfaceTexture;
    if (loaded)
    {
        *loaded = usable;
    }
    if (!usable)
    {
        if (id != 0)
        {
            const auto& source = mResourceManager->GetTexture(id);
            if ((source.state == ResourceState::Failed || found != mTextures.end()) &&
                mReportedSurfaceTextures.insert(id).second)
            {
                mSurfaceDiagnostic = source.name + ": " +
                    (source.error.empty() ? "material requires a sampleable 2D texture" : source.error);
                std::cerr << "Material texture fallback: " << mSurfaceDiagnostic << '\n';
            }
        }
        found = mTextures.find(std::to_string(fallback));
    }

    const auto& texture = *found->second;
    const bool nativeSrgb = IsSrgbView(texture.viewFormat);
    format = texture.viewFormat;
    if (srgb != nativeSrgb && texture.alternateSrvIndex >= 0)
    {
        // Only the transfer function differs between the two views.
        format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        return texture.alternateSrvIndex;
    }
    return texture.SrvHeapIndex;
}

MaterialGPU* D3DRenderAdapter::GetOrLoadMaterial(MaterialID materialId)
{
    if (!mResourceManager)
    {
        return nullptr;
    }
    const auto& parameters = mResourceManager->GetMaterial(materialId).parameters;
    auto& gpu = mMaterials[std::to_string(materialId)];
    if (!gpu)
    {
        gpu = std::make_unique<MaterialGPU>();
    }
    const TextureID white = mResourceManager->GetWhiteTexture();
    const TextureID checker = mResourceManager->GetPlaceholderTexture();
    DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT normalFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT ormFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT emissiveFormat = DXGI_FORMAT_UNKNOWN;
    gpu->DiffuseSrvHeapIndex = SurfaceTextureDescriptor(parameters.albedo, parameters.albedo ? checker : white, true, colorFormat);
    gpu->NormalSrvHeapIndex = SurfaceTextureDescriptor(parameters.normal, mResourceManager->GetFlatNormalTexture(), false, normalFormat);
    gpu->ormSrvIndex = SurfaceTextureDescriptor(parameters.orm, white, false, ormFormat, &gpu->hasORM);
    gpu->emissiveSrvIndex = SurfaceTextureDescriptor(parameters.emissive, parameters.emissive ? checker : white, true, emissiveFormat);
    gpu->textureFlags = 0;
    if (!IsSrgbView(colorFormat))
    {
        gpu->textureFlags |= 1u;
    }
    if (!IsSrgbView(emissiveFormat))
    {
        gpu->textureFlags |= 2u;
    }
    if (normalFormat == DXGI_FORMAT_BC5_UNORM || normalFormat == DXGI_FORMAT_BC5_SNORM ||
        normalFormat == DXGI_FORMAT_R8G8_UNORM || normalFormat == DXGI_FORMAT_R8G8_SNORM)
    {
        gpu->textureFlags |= 4u;
    }
    if (normalFormat == DXGI_FORMAT_BC5_SNORM || normalFormat == DXGI_FORMAT_R8G8_SNORM)
    {
        gpu->textureFlags |= 8u;
    }
    return gpu.get();
}
