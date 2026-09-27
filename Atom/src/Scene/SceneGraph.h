#pragma once
#include "Model.h"
#include "Material.h"
struct Scene
{
	std::vector<Model> Models;
	std::vector<UINT> MaterialIndex;
};
