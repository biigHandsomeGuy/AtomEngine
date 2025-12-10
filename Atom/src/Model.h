#pragma once

#include "tinygltf/tiny_gltf.h"
#include "Math/Matrix4.h"
#include "Math/Vector.h"
#include "DescriptorHeap.h"
#include "TextureManager.h"

using namespace DirectX;
using namespace Math;
using namespace Microsoft::WRL;
__declspec(align(256)) struct MeshConstants
{
	Math::Matrix4 ModelMatrix{ Math::kIdentity };
	Math::Matrix4 NormalMatrix{ Math::kIdentity };
};
struct Component
{
	OrthogonalTransform Transform{ kIdentity };
	Vector3 Scaling{ kIdentity };
};

struct Vertex
{
	Vector3 Position;
	Vector3 Normal;
	
	XMFLOAT2 UV;
	Vector3 Tangent;
	Vector3 BiTangent;
};


struct Material
{
	TextureRef Albedo;
	TextureRef Normal;
	TextureRef Metallic;
	TextureRef Roughness;
	TextureRef Occlusion;
	TextureRef Emissive;


	Vector4 BaseColorFactor{ 1.0f,1.0f,1.0f,1.0f };
	float MetallicFactor = 1.0f;
	float RoughnessFactor = 1.0f;
	Vector3 EmissiveFactor{ 0.0f,0.0f,0.0f };


	bool DoubleSided = false;
	bool Unlit = false;


	uint32_t PipelineStateID = 0;
	std::string Name;
};

struct Submesh
{
	uint32_t IndexCount;
	uint32_t StartIndex;
	uint32_t BaseVertex;

	BoundingBox Bounds;

	Material Material;
};


struct Mesh
{
	std::string Name;

	std::vector<Vertex>       CPUVertices;
	std::vector<uint32_t>     CPUIndices;

	D3D12_VERTEX_BUFFER_VIEW VBV{};
	D3D12_INDEX_BUFFER_VIEW  IBV{};

	uint32_t VertexCount = 0;
	uint32_t IndexCount = 0;

	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;

	
	std::vector<Submesh> Submeshes;

	DirectX::BoundingBox Bounds;
};

struct Node
{
	int32_t Parent = -1;
	std::vector<int32_t> Children;


	Matrix4 LocalMatrix{ kIdentity };
	Matrix4 WorldMatrix{ kIdentity };


	int32_t MeshIndex = -1;
	int32_t SkinIndex = -1;
	std::string Name;
};


struct Skin
{
	std::vector<int32_t> Joints; // node indices
	std::vector<Matrix4> InverseBindMatrices;
	int32_t SkeletonRoot = -1;
};


struct AnimationSampler
{
	std::vector<float> Inputs;
	std::vector<Vector3> Translations;
	std::vector<Quaternion> Rotations;
	std::vector<Vector3> Scales;
};


struct AnimationChannel
{
	int32_t SamplerIndex;
	int32_t TargetNode;
	enum class TargetPath { Translation, Rotation, Scale } Path;
};


struct Animation
{
	std::string Name;
	std::vector<AnimationSampler> Samplers;
	std::vector<AnimationChannel> Channels;
	float StartTime = 0.f;
	float EndTime = 0.f;
};

struct Model
{
	std::vector<Mesh> Meshes;
	std::vector<struct Material> Materials;
	std::vector<DescriptorHandle> MaterialSRVs;
	std::vector<Node> Nodes;
	std::vector<Skin> Skins;
	std::vector<Animation> Animations;
	BoundingBox Bounds;
	std::string Name;

	void CreateMaterialSRVs();
	void Draw(ID3D12GraphicsCommandList* cmdList, bool isSkyBox = false);

	void UpdateConstants()
	{
		//m_MeshConstants.ModelMatrix = Matrix4{ kIdentity };
		Matrix4 scaleMat = Matrix4::MakeScale(m_MeshComponent.Scaling);

		// 构建最终 M：Transform * S
		// 假设 Transform = T * R
		m_MeshConstants.ModelMatrix = m_MeshComponent.Transform;
		m_MeshConstants.ModelMatrix = m_MeshConstants.ModelMatrix * scaleMat;

		m_MeshConstants.NormalMatrix =
			Math::InverseTranspose(m_MeshConstants.ModelMatrix.Get3x3());
	}


	MeshConstants m_MeshConstants;

	Component m_MeshComponent;
};
void UploadMeshToGPU(Mesh& mesh);

Model LoadGltfModel(const std::string& path);

Material ConvertMaterial(
	const tinygltf::Model& gltf,
	const tinygltf::Material& gm,
	const std::string& baseDir);
