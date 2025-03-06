#pragma once

#include "Mesh.h"

struct aiMesh;
struct aiNode;
struct aiScene;

class Model
{
public:
	void Load(const std::string& filepath, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
	void Draw(ID3D12GraphicsCommandList* commandList);
	std::vector<Mesh> meshes;
	DirectX::XMMATRIX modelMatrix;
	DirectX::XMMATRIX normalMatrix;
private:

	Mesh ProcessMesh(aiMesh* mesh, const DirectX::XMMATRIX& transform, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
	void ProcessNode(aiNode* node, const aiScene* scene, const DirectX::XMMATRIX& parentTransform, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
};