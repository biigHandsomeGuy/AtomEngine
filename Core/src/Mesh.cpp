#include "Mesh.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include <stdexcept>
#include "d3dUtil.h"

using namespace DirectX;

namespace
{
    const unsigned int ImportFlags =
        aiProcess_CalcTangentSpace | aiProcess_Triangulate;
}


void Model::Load(const std::string& filepath, ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(filepath, ImportFlags);
	std::string extensions;
	importer.GetExtensionList(extensions);
	OutputDebugStringA(extensions.c_str());

	if (!scene || !scene->HasMeshes()) {
		throw std::runtime_error("Failed to load model: " + filepath);
	}

	meshes.clear();
	ProcessNode(scene->mRootNode, scene, XMMatrixIdentity(), device, commandList);
}

//void Model::Render(ID3D12GraphicsCommandList* commandList)
//{
//	for (const auto& mesh : meshes) {
//		// 设置变换矩阵到常量缓冲区（此部分根据你的实现设置变换）
//		SetTransform(mesh.Transform, commandList);
//
//		// 绑定缓冲区并绘制
//		commandList->IASetVertexBuffers(0, 1, &mesh.VertexBufferView);
//		commandList->IASetIndexBuffer(&mesh.IndexBufferView);
//		commandList->DrawIndexedInstanced(static_cast<UINT>(mesh.Indices.size()), 1, 0, 0, 0);
//	}
//
//}

void Model::ProcessNode(aiNode* node, const aiScene* scene, const XMMATRIX& parentTransform, ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
{
	aiMatrix4x4 aiTransform = node->mTransformation;
	XMMATRIX localTransform = XMMatrixTranspose(XMMATRIX(
		aiTransform.a1, aiTransform.b1, aiTransform.c1, aiTransform.d1,
		aiTransform.a2, aiTransform.b2, aiTransform.c2, aiTransform.d2,
		aiTransform.a3, aiTransform.b3, aiTransform.c3, aiTransform.d3,
		aiTransform.a4, aiTransform.b4, aiTransform.c4, aiTransform.d4));
	XMMATRIX globalTransform = XMMatrixMultiply(localTransform, parentTransform);

	for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		meshes.push_back(ProcessMesh(mesh, globalTransform, device, commandList));
	}

	for (unsigned int i = 0; i < node->mNumChildren; ++i) {
		ProcessNode(node->mChildren[i], scene, globalTransform, device, commandList);
	}
}

Mesh Model::ProcessMesh(aiMesh* mesh, const XMMATRIX& transform, ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
{
	Mesh newMesh;
	//newMesh.Transform = transform;

	// 提取顶点数据
	for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
		Vertex vertex;
		vertex.Position = XMFLOAT3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
		vertex.Normal = XMFLOAT3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
		if (mesh->HasTangentsAndBitangents()) {
			vertex.Tangent = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
			vertex.BiTangent = { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z };
		}
		if (mesh->HasTextureCoords(0)) {
			vertex.TexCoords = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
		}
		newMesh.Vertices.push_back(vertex);
	}

	// 提取索引数据
	for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
		const aiFace& face = mesh->mFaces[i];
		for (unsigned int j = 0; j < face.mNumIndices; ++j) {
			newMesh.Indices.push_back(face.mIndices[j]);
		}
	}

	const size_t vertexDataSize = newMesh.Vertices.size() * sizeof(Vertex);
	const size_t indexDataSize = newMesh.Indices.size() * sizeof(UINT);

	// create GPU resource
	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexDataSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&newMesh.vertexBuffer)
	));


	newMesh.vbv.BufferLocation = newMesh.vertexBuffer->GetGPUVirtualAddress();
	newMesh.vbv.SizeInBytes = static_cast<UINT>(vertexDataSize);
	newMesh.vbv.StrideInBytes = sizeof(Vertex);

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES{ D3D12_HEAP_TYPE_UPLOAD },
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexDataSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&newMesh.indexBuffer)));

	newMesh.ibv.BufferLocation = newMesh.indexBuffer->GetGPUVirtualAddress();
	newMesh.ibv.SizeInBytes = static_cast<UINT>(indexDataSize);
	newMesh.ibv.Format = DXGI_FORMAT_R32_UINT;

	UINT8* data = nullptr;
	// copy vertex
	ThrowIfFailed(newMesh.vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data)));
	memcpy(data, newMesh.Vertices.data(), vertexDataSize);
	newMesh.vertexBuffer->Unmap(0, nullptr);

	// copy index
	newMesh.indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
	memcpy(data, newMesh.Indices.data(), indexDataSize);
	newMesh.indexBuffer->Unmap(0, nullptr);

	return newMesh;
}

