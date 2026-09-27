#pragma once

class Texture;

struct PbrMaterial
{
	Texture* Albedo;
	Texture* Normal;
	Texture* Metallic;
	Texture* Roughness;
};
