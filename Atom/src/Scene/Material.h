#pragma once

struct Texture;

struct PbrMaterial
{
	Texture* Albedo;
	Texture* Normal;
	Texture* Metallic;
	Texture* Roughness;
};