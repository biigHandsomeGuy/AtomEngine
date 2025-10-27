#pragma once

#include "tinyobjloader/tiny_obj_loader.h"
#include "Mesh.h"
#include "Math/Matrix4.h"
#include "DescriptorHeap.h"
#include "TextureManager.h"

struct tinyobj::attrib_t;
struct tinyobj::shape_t;

class Model
{
public:
	void Load(const std::wstring& filepath, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
	void Draw(ID3D12GraphicsCommandList* commandList);
	std::vector<Mesh> meshes;
	Math::Matrix4 modelMatrix;
	Math::Matrix4 normalMatrix;

	void LoadTextures(const std::wstring& basePath);
private:
	Mesh ProcessMesh(const tinyobj::attrib_t& attrib, const tinyobj::shape_t& shape, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);

private:
	std::vector<TextureRef> m_TextureReferences;

	DescriptorHandle m_SRVs;
	uint32_t m_SRVDescriptorSize;
};