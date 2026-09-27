#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <cmath>

class Texture;

struct PbrMaterial
{
	Texture* Albedo;
	Texture* Normal;
	Texture* Metallic;
	Texture* Roughness;
};

// Values belong to an immutable imported material or a scene-instance override.
// Texture flags: base color, normal, metallic/roughness, emissive.
struct MaterialParameters
{
    DirectX::XMFLOAT4 BaseColorFactor{ 1, 1, 1, 1 };
    DirectX::XMFLOAT3 EmissiveFactor{ 0, 0, 0 };
    float NormalScale = 1;
    float MetallicFactor = 1;
    float RoughnessFactor = 1;
    uint32_t TextureFlags = 15;
};

inline bool ValidMaterialParameters(const MaterialParameters& material)
{
    const float values[] = { material.BaseColorFactor.x, material.BaseColorFactor.y,
        material.BaseColorFactor.z, material.BaseColorFactor.w, material.EmissiveFactor.x,
        material.EmissiveFactor.y, material.EmissiveFactor.z, material.NormalScale,
        material.MetallicFactor, material.RoughnessFactor };
    for (float value : values) if (!std::isfinite(value)) return false;
    const auto& color = material.BaseColorFactor;
    return color.x >= 0 && color.x <= 1 && color.y >= 0 && color.y <= 1 &&
        color.z >= 0 && color.z <= 1 && color.w >= 0 && color.w <= 1 &&
        material.EmissiveFactor.x >= 0 && material.EmissiveFactor.y >= 0 && material.EmissiveFactor.z >= 0 &&
        material.MetallicFactor >= 0 && material.MetallicFactor <= 1 &&
        material.RoughnessFactor >= 0 && material.RoughnessFactor <= 1 && material.TextureFlags <= 15;
}
