#pragma once
#include <array>
#include <compare>
#include <stdexcept>
#include <string>
#include <string_view>

enum class DepthComparison
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

enum class BlendFactor
{
    Zero,
    One,
    SrcColor,
    InvSrcColor,
    SrcAlpha,
    InvSrcAlpha,
    DestColor,
    InvDestColor,
    DestAlpha,
    InvDestAlpha
};

enum class BlendOperation
{
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};

inline constexpr std::array DepthComparisonNames = {
    "Never", "Less", "Equal", "LessEqual", "Greater", "NotEqual", "GreaterEqual", "Always"};
inline constexpr std::array BlendFactorNames = {
    "Zero", "One", "SrcColor", "InvSrcColor", "SrcAlpha", "InvSrcAlpha",
    "DestColor", "InvDestColor", "DestAlpha", "InvDestAlpha"};
inline constexpr std::array BlendOperationNames = {"Add", "Subtract", "ReverseSubtract", "Min", "Max"};

struct MaterialRenderState
{
    bool depthTest = true;
    bool depthWrite = true;
    DepthComparison depthCompare = DepthComparison::Less;
    bool blendEnabled = false;
    BlendFactor sourceColor = BlendFactor::One;
    BlendFactor destinationColor = BlendFactor::Zero;
    BlendOperation colorOperation = BlendOperation::Add;
    BlendFactor sourceAlpha = BlendFactor::One;
    BlendFactor destinationAlpha = BlendFactor::Zero;
    BlendOperation alphaOperation = BlendOperation::Add;

    auto operator<=>(const MaterialRenderState&) const = default;

    static MaterialRenderState AlphaBlend()
    {
        MaterialRenderState state;
        state.depthWrite = false;
        state.blendEnabled = true;
        state.sourceColor = BlendFactor::SrcAlpha;
        state.destinationColor = BlendFactor::InvSrcAlpha;
        state.destinationAlpha = BlendFactor::InvSrcAlpha;
        return state;
    }

    // RGB = source.rgb * source.a + destination.rgb. Alpha accumulates too.
    static MaterialRenderState Additive()
    {
        MaterialRenderState state = AlphaBlend();
        state.destinationColor = BlendFactor::One;
        state.destinationAlpha = BlendFactor::One;
        return state;
    }

    void Validate() const
    {
        const auto check = [](auto value, const auto& names, const char* field)
        {
            if (static_cast<size_t>(value) >= names.size())
            {
                throw std::invalid_argument(std::string("surface.renderState.") + field + ": invalid enum value");
            }
        };
        check(depthCompare, DepthComparisonNames, "depthCompare");
        check(sourceColor, BlendFactorNames, "sourceColor");
        check(destinationColor, BlendFactorNames, "destinationColor");
        check(colorOperation, BlendOperationNames, "colorOperation");
        check(sourceAlpha, BlendFactorNames, "sourceAlpha");
        check(destinationAlpha, BlendFactorNames, "destinationAlpha");
        check(alphaOperation, BlendOperationNames, "alphaOperation");
    }
};
