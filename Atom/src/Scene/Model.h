#pragma once

#include "d3d12.h"
#include "Math/Matrix4.h"
#include "Math/Vector.h"
#include "Math/BoundingBox.h"

#include "tinygltf/tiny_gltf.h"

#include "DescriptorHeap.h"
#include "TextureManager.h"
#include "Material.h"
#include <map>
#include <cstdint>
#include <atomic>
#include <memory>
#include <wrl/client.h>

class GraphicsContext;
class CommandContext;
struct MaterialConstants;

using AABB = Math::AxisAlignedBox;

using namespace DirectX;
using namespace Math;

using TextureID = uint32_t;
using EntityId = uint64_t;

static constexpr uint32_t INVALID_INDEX = 0xffffffff;
static constexpr EntityId InvalidEntityId = 0;

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

    uint32_t BaseColor = INVALID_INDEX;

    uint32_t Normal = INVALID_INDEX;

    uint32_t RoughnessMetallic = INVALID_INDEX;

    uint32_t Emissive = INVALID_INDEX;

    MaterialParameters Parameters;
};


// GPU material
struct MaterialGPU
{
    uint32_t BaseColorIndex = INVALID_TEXTURE;
    uint32_t NormalIndex = INVALID_TEXTURE;
    uint32_t RoughnessMetallicIndex = INVALID_TEXTURE;
    uint32_t EmissiveIndex = INVALID_TEXTURE;
    uint32_t TextureFlags = 0;
};



// mesh instance
struct MeshInstance
{
    uint32_t MeshIndex = 0;

    MeshConstants Constants;
};

struct ImageAsset
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool Srgb = false;
    std::vector<uint8_t> Pixels; // Decoded RGBA8; no GPU handles.
};

// Model Asset (immutable). Material texture fields index Images.
struct ModelAsset
{
    std::string Name;

    // Original source file used to build this reusable model asset.
    std::string SourcePath;

    std::vector<MeshAsset> Meshes;

    std::vector<MaterialAsset> Materials;
    std::vector<ImageAsset> Images;

    AABB Bounds;
};


// GPU representation
struct ModelGPU
{
    std::vector<MeshGPU> Meshes;

    std::vector<MaterialGPU> Materials;
    std::vector<TextureID> Textures;
    std::vector<std::shared_ptr<const TextureResidency>> TextureOwners;
    uint64_t ResidentBytes = 0; // D3D12 committed geometry and image allocations.
};


// runtime instance
class Model
{
public:

    EntityId Id = InvalidEntityId;
    std::string InstanceName;

    std::shared_ptr<const ModelAsset> Asset;

    std::shared_ptr<const ModelGPU> GPU;

    TransformComponent Transform;

    // Overrides are local to this scene instance; GPU geometry/images stay shared.
    std::map<uint32_t, MaterialParameters> MaterialOverrides;
    MaterialParameters MaterialValues(uint32_t index) const;
    MaterialConstants MaterialData(uint32_t index) const;
    bool SetMaterialOverride(uint32_t index, const MaterialParameters& parameters);
    bool ResetMaterialOverride(uint32_t index);

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
    std::shared_ptr<const ModelAsset> Asset;
    std::shared_ptr<const ModelGPU> GPU;
};

namespace ModelUtility
{
    // CPU-only decoding is safe on an importer worker; upload runs on the render thread.
    std::shared_ptr<const ModelAsset> DecodeGltfAsset(const std::string& path,
        std::string* error = nullptr, std::string* warnings = nullptr,
        const std::atomic_bool* cancelled = nullptr);
    std::shared_ptr<const ModelGPU> UploadAsset(const ModelAsset& asset, CommandContext& context);
    LoadedModelData LoadGltfAsset(const std::string& path, std::string* error = nullptr);
    Model CreateModel(LoadedModelData&& data);

}
