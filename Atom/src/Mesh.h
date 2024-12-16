#pragma once

#include "Texture.h"

#include <d3d12.h>
#include <DirectXMath.h>
#include <vector>
#include <memory>
#include <string>
#include "Atom/Core.h"

struct Vertex
{
	DirectX::XMFLOAT3 Position;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexCoords;
	DirectX::XMFLOAT3 Tangent;
	DirectX::XMFLOAT3 BiTangent;
};

class Mesh
{
public:
	// std::vector<Texture> textures;
	std::vector<Vertex> Vertices;
	std::vector<UINT> Indices;
	ComPtr<ID3D12Resource> vertexBuffer;
	ComPtr<ID3D12Resource> indexBuffer;

	D3D12_VERTEX_BUFFER_VIEW vbv;
	D3D12_INDEX_BUFFER_VIEW ibv;

	//DirectX::XMMATRIX Transform;
};

struct aiMesh;
struct aiNode;
struct aiScene;

class Model
{
public:
	void Load(const std::string& filepath, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
	void Render(ID3D12GraphicsCommandList* commandList);
	std::vector<Mesh> meshes;
private:
	
	Mesh ProcessMesh(aiMesh* mesh, const DirectX::XMMATRIX& transform, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
	void ProcessNode(aiNode* node, const aiScene* scene, const DirectX::XMMATRIX& parentTransform, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
};