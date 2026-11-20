#include "D3DRenderAdapter.h"
#include "AssetPaths.h"
#include <d3d12shader.h>
#include <filesystem>
#include <cstring>

namespace
{
    D3D12_COMPARISON_FUNC ToDepthComparison(DepthComparison comparison)
    {
        constexpr D3D12_COMPARISON_FUNC values[] = {
            D3D12_COMPARISON_FUNC_NEVER, D3D12_COMPARISON_FUNC_LESS, D3D12_COMPARISON_FUNC_EQUAL,
            D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_COMPARISON_FUNC_GREATER, D3D12_COMPARISON_FUNC_NOT_EQUAL,
            D3D12_COMPARISON_FUNC_GREATER_EQUAL, D3D12_COMPARISON_FUNC_ALWAYS};
        return values[static_cast<size_t>(comparison)];
    }

    D3D12_BLEND ToBlendFactor(BlendFactor factor, bool alpha = false)
    {
        // Color factors on the scalar alpha channel mean their alpha component.
        constexpr D3D12_BLEND colorValues[] = {
            D3D12_BLEND_ZERO, D3D12_BLEND_ONE, D3D12_BLEND_SRC_COLOR, D3D12_BLEND_INV_SRC_COLOR,
            D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_DEST_COLOR,
            D3D12_BLEND_INV_DEST_COLOR, D3D12_BLEND_DEST_ALPHA, D3D12_BLEND_INV_DEST_ALPHA};
        constexpr D3D12_BLEND alphaValues[] = {
            D3D12_BLEND_ZERO, D3D12_BLEND_ONE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA,
            D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_DEST_ALPHA,
            D3D12_BLEND_INV_DEST_ALPHA, D3D12_BLEND_DEST_ALPHA, D3D12_BLEND_INV_DEST_ALPHA};
        return alpha ? alphaValues[static_cast<size_t>(factor)] : colorValues[static_cast<size_t>(factor)];
    }

    D3D12_BLEND_OP ToBlendOperation(BlendOperation operation)
    {
        constexpr D3D12_BLEND_OP values[] = {
            D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_SUBTRACT, D3D12_BLEND_OP_REV_SUBTRACT,
            D3D12_BLEND_OP_MIN, D3D12_BLEND_OP_MAX};
        return values[static_cast<size_t>(operation)];
    }

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

size_t D3DRenderAdapter::SurfacePipelineHash::operator()(const SurfacePipelineKey& key) const
{
    size_t result = 0;
    const auto combine = [&](const auto& value)
    {
        result ^= std::hash<std::decay_t<decltype(value)>>{}(value) + size_t(0x9e3779b9) + (result << 6) + (result >> 2);
    };
    combine(key.program.file);
    combine(key.program.vertexEntry);
    combine(key.program.pixelEntry);
    combine(key.program.editorVariant);
    combine(key.program.maskedVariant);
    combine(key.mode);
    combine(key.twoSided);
    const auto& state = key.renderState;
    combine(state.depthTest);
    combine(state.depthWrite);
    combine(state.depthCompare);
    combine(state.blendEnabled);
    combine(state.sourceColor);
    combine(state.destinationColor);
    combine(state.colorOperation);
    combine(state.sourceAlpha);
    combine(state.destinationAlpha);
    combine(state.alphaOperation);
    combine(key.colorFormat);
    combine(key.identifierFormat);
    combine(key.depthFormat);
    combine(key.samples);
    combine(key.sampleQuality);
    return result;
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
    key.renderState = base.renderState;
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
    key.renderState.Validate();
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
    const auto& state = key.renderState;
    description.BlendState.IndependentBlendEnable = TRUE;
    auto& colorBlend = description.BlendState.RenderTarget[0];
    colorBlend.BlendEnable = state.blendEnabled;
    colorBlend.SrcBlend = ToBlendFactor(state.sourceColor);
    colorBlend.DestBlend = ToBlendFactor(state.destinationColor);
    colorBlend.BlendOp = ToBlendOperation(state.colorOperation);
    colorBlend.SrcBlendAlpha = ToBlendFactor(state.sourceAlpha, true);
    colorBlend.DestBlendAlpha = ToBlendFactor(state.destinationAlpha, true);
    colorBlend.BlendOpAlpha = ToBlendOperation(state.alphaOperation);
    // RT1 is R32_UINT: its default disabled blending must remain independent of RT0.
    description.DepthStencilState.DepthEnable = state.depthTest || state.depthWrite;
    description.DepthStencilState.DepthWriteMask = state.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    description.DepthStencilState.DepthFunc = state.depthTest ? ToDepthComparison(state.depthCompare) : D3D12_COMPARISON_FUNC_ALWAYS;
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

uint64_t D3DRenderAdapter::TexturePreview(TextureID texture)
{
    if (!texture || !mResourceManager)
    {
        return 0;
    }
    const auto existing = mTextures.find(std::to_string(texture));
    if (existing != mTextures.end() && existing->second->surfaceTexture)
    {
        const auto& gpu = *existing->second;
        const int descriptor = IsSrgbView(gpu.viewFormat) && gpu.alternateSrvIndex >= 0
            ? gpu.alternateSrvIndex : gpu.SrvHeapIndex;
        return GetGPUDescriptorHandle(descriptor).ptr;
    }
    const auto preview = mTexturePreviews.find(texture);
    if (preview != mTexturePreviews.end())
    {
        return preview->second ? GetGPUDescriptorHandle(preview->second->SrvHeapIndex).ptr : 0;
    }
    mPendingTexturePreviews.insert(texture);
    return 0;
}

void D3DRenderAdapter::PumpTexturePreviews()
{
    // UI requests arrive before BeginFrame. Record uploads only on the reset frame list.
    size_t uploaded = 0;
    for (auto iterator = mPendingTexturePreviews.begin(); iterator != mPendingTexturePreviews.end() && uploaded < 2;)
    {
        const TextureID textureID = *iterator;
        const auto& texture = mResourceManager->GetTexture(textureID);
        if (texture.state == ResourceState::Loading)
        {
            ++iterator;
            continue;
        }
        iterator = mPendingTexturePreviews.erase(iterator);
        const auto& image = texture.imageData;
        if (texture.state == ResourceState::Failed || image.subresources.empty() || image.pixels.empty())
        {
            continue;
        }
        const auto existing = mTextures.find(std::to_string(textureID));
        if (existing != mTextures.end() && existing->second->surfaceTexture)
        {
            continue;
        }
        if (image.dimension == ImageDimension::Texture2D && image.arraySize == 1 && !image.isCubeMap)
        {
            continue; // The normal upload queue supplies the shared 2D SRV.
        }
        ++uploaded;
        DXGI_FORMAT format = image.format;
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            format = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            format = DXGI_FORMAT_B8G8R8A8_UNORM;
            break;
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            format = DXGI_FORMAT_B8G8R8X8_UNORM;
            break;
        case DXGI_FORMAT_BC1_UNORM_SRGB:
            format = DXGI_FORMAT_BC1_UNORM;
            break;
        case DXGI_FORMAT_BC2_UNORM_SRGB:
            format = DXGI_FORMAT_BC2_UNORM;
            break;
        case DXGI_FORMAT_BC3_UNORM_SRGB:
            format = DXGI_FORMAT_BC3_UNORM;
            break;
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            format = DXGI_FORMAT_BC7_UNORM;
            break;
        default:
            break;
        }
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
        if (mNextCbvSrvIndex >= mCbvSrvUavDescriptorCount ||
            FAILED(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
            !(support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE))
        {
            mTexturePreviews.emplace(textureID, nullptr);
            continue;
        }
        uint32_t mip = 0;
        while (mip + 1 < image.mipLevels && std::max(image.width >> mip, image.height >> mip) > 128)
        {
            ++mip;
        }
        const auto& source = image.subresources.at(mip);
        // The first array element/cubemap face or volume slice is a regular 2D thumbnail.
        const auto description = CD3DX12_RESOURCE_DESC::Tex2D(format,
            std::max(1u, image.width >> mip), std::max(1u, image.height >> mip), 1, 1);
        auto preview = std::make_unique<TextureGPU>();
        const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        if (FAILED(md3dDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&preview->Resource))))
        {
            continue;
        }
        const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto uploadDescription = CD3DX12_RESOURCE_DESC::Buffer(GetRequiredIntermediateSize(preview->Resource.Get(), 0, 1));
        if (FAILED(md3dDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDescription,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&preview->UploadHeap))))
        {
            continue;
        }
        D3D12_SUBRESOURCE_DATA data{image.pixels.data() + source.offset,
            static_cast<LONG_PTR>(source.rowPitch), static_cast<LONG_PTR>(source.slicePitch)};
        UpdateSubresources(mCommandList.Get(), preview->Resource.Get(), preview->UploadHeap.Get(), 0, 0, 1, &data);
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(preview->Resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        mCommandList->ResourceBarrier(1, &barrier);
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        const auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
            mNextCbvSrvIndex, mCbvSrvUavDescriptorSize);
        md3dDevice->CreateShaderResourceView(preview->Resource.Get(), &view, handle);
        preview->SrvHeapIndex = static_cast<int>(mNextCbvSrvIndex++);
        preview->uploadCompleteFence = mCurrentFence + 1;
        mTexturePreviews.emplace(textureID, std::move(preview));
    }
}
