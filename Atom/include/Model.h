#pragma once

#include "tinyobjloader/tiny_obj_loader.h"

#include "Mesh.h"

struct tinyobj::attrib_t;
struct tinyobj::shape_t;

class Model
{
public:
	void Load(const std::string& filepath, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
	void Draw(ID3D12GraphicsCommandList* commandList);
	std::vector<Mesh> meshes;
	DirectX::XMMATRIX modelMatrix;
	DirectX::XMMATRIX normalMatrix;
private:

	Mesh ProcessMesh(const tinyobj::attrib_t& attrib, const tinyobj::shape_t& shape, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
};