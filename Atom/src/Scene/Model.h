#pragma once

#include "d3d12.h"
#include "Math/Matrix4.h"
#include "Math/Vector.h"
#include "Math/BoundingBox.h"

#include "tinygltf/tiny_gltf.h"

#include "DescriptorHeap.h"
#include "TextureManager.h"
#include <memory>
#include <wrl/client.h>

class GraphicsContext;

using AABB = Math::AxisAlignedBox;

using namespace DirectX;
using namespace Math;

using TextureID = uint32_t;

static constexpr uint32_t INVALID_INDEX = 0xffffffff;

// per-instance constants (GPU CBV)
__declspec(align(256))
struct MeshConstants
{
    Matrix4 ModelMatrix{ kIdentity };
    Matrix4 NormalMatrix{ kIdentity };
};

// transform component
struct TransformComponent
{
    OrthogonalTransform transform;

    float scale = 1.0f;

    Matrix4 GetMatrix() const
    {
        AffineTransform RT(transform);

        AffineTransform S = AffineTransform::MakeScale(scale);

        return RT * S;
    }
};


// vertex layout (gltf compatible)
struct Vertex
{
    Vector3 Position;

    Vector3 Normal;

    XMFLOAT2 UV;

    Vector3 Tangent;

    Vector3 Bitangent;

    uint32_t ClusterID = 0;
};

// SubMesh = gltf primitive
struct SubMesh
{
    uint32_t IndexOffset = 0;

    uint32_t IndexCount = 0;

    uint32_t MaterialIndex = 0;

    AABB Bounds;
};

// CPU asset
struct MeshAsset
{
    std::string Name;

    std::vector<Vertex> Vertices;

    std::vector<uint32_t> Indices;

    std::vector<SubMesh> SubMeshes;

    AABB Bounds;
};

// GPU geometry
struct MeshGPU
{
    D3D12_VERTEX_BUFFER_VIEW VBV{};

    D3D12_INDEX_BUFFER_VIEW IBV{};

    uint32_t VertexCount = 0;

    uint32_t IndexCount = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;

    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;
};


// material asset (CPU)
struct MaterialAsset
{
    std::string Name;

    TextureID BaseColor = INVALID_TEXTURE;

    TextureID Normal = INVALID_TEXTURE;

    TextureID RoughnessMetallic = INVALID_TEXTURE;

    TextureID Emissive = INVALID_TEXTURE;

    Vector3 EmissiveFactor{ 0,0,0 };
};


// GPU material
struct MaterialGPU
{
    uint32_t BaseColorIndex = INVALID_TEXTURE;
    uint32_t NormalIndex = INVALID_TEXTURE;
    uint32_t RoughnessMetallicIndex = INVALID_TEXTURE;
    uint32_t EmissiveIndex = INVALID_TEXTURE;
    Vector3 EmissiveFactor;

    float padding;
};



// mesh instance
struct MeshInstance
{
    uint32_t MeshIndex = 0;

    MeshConstants Constants;
};

// Model Asset (immutable)
struct ModelAsset
{
    std::string Name;

    std::vector<MeshAsset> Meshes;

    std::vector<MaterialAsset> Materials;

    AABB Bounds;
};


// GPU representation
struct ModelGPU
{
    std::vector<MeshGPU> Meshes;

    std::vector<MaterialGPU> Materials;
};


// runtime instance
class Model
{
public:

    std::shared_ptr<ModelAsset> Asset;

    ModelGPU GPU;

    TransformComponent Transform;

    MeshConstants Constants;


    void UpdateConstants()
    {
        Constants.ModelMatrix = Transform.GetMatrix();

        Constants.NormalMatrix = InverseTranspose(Constants.ModelMatrix.Get3x3());
    }
    void Draw(GraphicsContext& context, bool bindMaterialData = true);
};

struct LoadedModelData
{
    std::shared_ptr<ModelAsset> Asset;
    ModelGPU GPU;
};

namespace ModelUtility
{
    LoadedModelData LoadGltfAsset(const std::string& path);
    Model CreateModel(LoadedModelData&& data);

}
