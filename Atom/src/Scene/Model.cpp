#include "pch.h"
#include "Model.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <filesystem>
#include <vector>

#include "GraphicsCore.h"
#include "CommandContext.h"
#include "Renderer.h"
#include "ConstantBuffers.h"

using namespace DirectX;

static void AccumulateSurfaceBasis(
    const Vector3& p0,
    const Vector3& p1,
    const Vector3& p2,
    const XMFLOAT2& uv0,
    const XMFLOAT2& uv1,
    const XMFLOAT2& uv2,
    Vector3& outNormal,
    Vector3& outTangent,
    Vector3& outBitangent)
{
    const Vector3 e1 = p1 - p0;
    const Vector3 e2 = p2 - p0;
    const Vector3 faceNormal = Cross(e1, e2);
    outNormal += faceNormal;

    const float du1 = uv1.x - uv0.x;
    const float dv1 = uv1.y - uv0.y;
    const float du2 = uv2.x - uv0.x;
    const float dv2 = uv2.y - uv0.y;
    const float det = du1 * dv2 - dv1 * du2;

    if (std::abs(det) > 1e-8f)
    {
        const float invDet = 1.0f / det;
        outTangent += (e1 * dv2 - e2 * dv1) * invDet;
        outBitangent += (e2 * du1 - e1 * du2) * invDet;
    }
}

static void RebuildVertexNormalsAndTangents(MeshAsset& mesh, bool rebuildNormals, bool rebuildTangents)
{
    if (mesh.Vertices.empty() || mesh.Indices.size() < 3)
        return;

    std::vector<Vector3> normalAccum(mesh.Vertices.size(), Vector3(kZero));
    std::vector<Vector3> tangentAccum(mesh.Vertices.size(), Vector3(kZero));
    std::vector<Vector3> bitangentAccum(mesh.Vertices.size(), Vector3(kZero));

    for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
    {
        const uint32_t i0 = mesh.Indices[i + 0];
        const uint32_t i1 = mesh.Indices[i + 1];
        const uint32_t i2 = mesh.Indices[i + 2];

        if (i0 >= mesh.Vertices.size() || i1 >= mesh.Vertices.size() || i2 >= mesh.Vertices.size())
            continue;

        const Vertex& v0 = mesh.Vertices[i0];
        const Vertex& v1 = mesh.Vertices[i1];
        const Vertex& v2 = mesh.Vertices[i2];

        Vector3 faceNormal(kZero);
        Vector3 faceTangent(kZero);
        Vector3 faceBitangent(kZero);
        AccumulateSurfaceBasis(
            v0.Position, v1.Position, v2.Position,
            v0.UV, v1.UV, v2.UV,
            faceNormal, faceTangent, faceBitangent);

        normalAccum[i0] += faceNormal;
        normalAccum[i1] += faceNormal;
        normalAccum[i2] += faceNormal;

        tangentAccum[i0] += faceTangent;
        tangentAccum[i1] += faceTangent;
        tangentAccum[i2] += faceTangent;

        bitangentAccum[i0] += faceBitangent;
        bitangentAccum[i1] += faceBitangent;
        bitangentAccum[i2] += faceBitangent;
    }

    for (size_t i = 0; i < mesh.Vertices.size(); ++i)
    {
        Vertex& v = mesh.Vertices[i];

        Vector3 normal = rebuildNormals ? normalAccum[i] : v.Normal;
        if (LengthSquare(normal) < 1e-8f)
            normal = Vector3(kZUnitVector);
        normal = Normalize(normal);

        Vector3 tangent = rebuildTangents ? tangentAccum[i] : v.Tangent;
        tangent = tangent - normal * Dot(normal, tangent);
        if (LengthSquare(tangent) < 1e-8f)
        {
            const Vector3 fallbackAxis = std::abs(normal.GetX()) < 0.999f ? Vector3(kXUnitVector) : Vector3(kYUnitVector);
            tangent = Normalize(Cross(fallbackAxis, normal));
        }
        else
        {
            tangent = Normalize(tangent);
        }

        Vector3 bitangent = rebuildTangents ? bitangentAccum[i] : v.Bitangent;
        bitangent = bitangent - normal * Dot(normal, bitangent) - tangent * Dot(tangent, bitangent);
        if (LengthSquare(bitangent) < 1e-8f)
            bitangent = Normalize(Cross(normal, tangent));
        else
            bitangent = Normalize(bitangent);

        if (Dot(Cross(normal, tangent), bitangent) < 0.0f)
            bitangent = -bitangent;

        v.Normal = normal;
        v.Tangent = tangent;
        v.Bitangent = bitangent;
    }
}

static Vector3 ReadVec3(const tinygltf::Model& model, const tinygltf::Accessor& accessor, uint32_t index)
{
    const auto& view = model.bufferViews[accessor.bufferView];
    const auto& buffer = model.buffers[view.buffer];

    const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    const float* ptr = reinterpret_cast<const float*>(data + index * accessor.ByteStride(view));

    return Vector3(ptr[0], ptr[1], ptr[2]);
}

static XMFLOAT2 ReadVec2(const tinygltf::Model& model, const tinygltf::Accessor& accessor, uint32_t index)
{
    const auto& view = model.bufferViews[accessor.bufferView];
    const auto& buffer = model.buffers[view.buffer];

    const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    const float* ptr = reinterpret_cast<const float*>(data + index * accessor.ByteStride(view));

    return XMFLOAT2(ptr[0], ptr[1]);
}

static XMFLOAT4 ReadVec4(const tinygltf::Model& model, const tinygltf::Accessor& accessor, uint32_t index)
{
    const auto& view = model.bufferViews[accessor.bufferView];
    const auto& buffer = model.buffers[view.buffer];

    const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    const float* ptr = reinterpret_cast<const float*>(data + index * accessor.ByteStride(view));

    return XMFLOAT4(ptr[0], ptr[1], ptr[2], ptr[3]);
}

static void LoadPrimitive(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    MeshAsset& mesh,
    bool& needsNormalRebuild,
    bool& needsTangentRebuild)
{
    const auto& posAcc = model.accessors[primitive.attributes.at("POSITION")];
    const tinygltf::Accessor* normAcc = nullptr;
    if (primitive.attributes.contains("NORMAL"))
        normAcc = &model.accessors[primitive.attributes.at("NORMAL")];

    const tinygltf::Accessor* uvAcc = nullptr;
    if (primitive.attributes.contains("TEXCOORD_0"))
        uvAcc = &model.accessors[primitive.attributes.at("TEXCOORD_0")];

    const tinygltf::Accessor* tangentAcc = nullptr;
    if (primitive.attributes.contains("TANGENT"))
        tangentAcc = &model.accessors[primitive.attributes.at("TANGENT")];

    needsNormalRebuild = needsNormalRebuild || (normAcc == nullptr);
    needsTangentRebuild = needsTangentRebuild || (tangentAcc == nullptr);

    const uint32_t vertexBase = static_cast<uint32_t>(mesh.Vertices.size());
    const uint32_t indexBase = static_cast<uint32_t>(mesh.Indices.size());
    const uint32_t vertexCount = static_cast<uint32_t>(posAcc.count);

    for (uint32_t i = 0; i < vertexCount; i++)
    {
        Vertex v{};
        v.Position = ReadVec3(model, posAcc, i);
        v.Normal = normAcc ? ReadVec3(model, *normAcc, i) : Vector3(kZero);
        v.UV = uvAcc ? ReadVec2(model, *uvAcc, i) : XMFLOAT2(0, 0);

        if (tangentAcc)
        {
            const XMFLOAT4 tangent = ReadVec4(model, *tangentAcc, i);
            v.Tangent = Vector3(tangent.x, tangent.y, tangent.z);
            v.Bitangent = Cross(v.Normal, v.Tangent) * tangent.w;
        }
        else
        {
            v.Tangent = Vector3(kZero);
            v.Bitangent = Vector3(kZero);
        }

        mesh.Vertices.push_back(v);
    }

    uint32_t primitiveIndexCount = 0;
    if (primitive.indices >= 0)
    {
        const auto& idxAcc = model.accessors[primitive.indices];
        const auto& view = model.bufferViews[idxAcc.bufferView];
        const auto& buffer = model.buffers[view.buffer];
        const uint8_t* data = buffer.data.data() + view.byteOffset + idxAcc.byteOffset;
        primitiveIndexCount = static_cast<uint32_t>(idxAcc.count);

        for (size_t i = 0; i < idxAcc.count; i++)
        {
            uint32_t index = 0;

            switch (idxAcc.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                index = reinterpret_cast<const uint16_t*>(data)[i];
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                index = reinterpret_cast<const uint32_t*>(data)[i];
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                index = reinterpret_cast<const uint8_t*>(data)[i];
                break;
            default:
                break;
            }

            mesh.Indices.push_back(index + vertexBase);
        }
    }
    else
    {
        // glTF primitive can be non-indexed; build a sequential index buffer.
        primitiveIndexCount = vertexCount;
        for (uint32_t i = 0; i < vertexCount; ++i)
            mesh.Indices.push_back(vertexBase + i);
    }

    SubMesh sub{};
    sub.IndexOffset = indexBase;
    sub.IndexCount = primitiveIndexCount;
    sub.MaterialIndex = primitive.material >= 0 ? static_cast<uint32_t>(primitive.material) : 0;
    mesh.SubMeshes.push_back(sub);
}

static void CreateBuffer(ID3D12Resource** resource, const void* data, uint64_t size)
{
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ASSERT_SUCCEEDED(Graphics::g_Device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(resource)));

    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);

    ASSERT_SUCCEEDED(Graphics::g_Device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(upload.GetAddressOf())));

    void* mapped = nullptr;
    upload->Map(0, nullptr, &mapped);
    memcpy(mapped, data, size);
    upload->Unmap(0, nullptr);

    CommandContext& ctx = CommandContext::Begin();
    ctx.GetCommandList()->CopyBufferRegion(*resource, 0, upload.Get(), 0, size);

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        *resource,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_GENERIC_READ);

    ctx.GetCommandList()->ResourceBarrier(1, &barrier);
    ctx.Finish(true);
}

static MeshGPU UploadMesh(const MeshAsset& asset)
{
    MeshGPU gpu{};

    uint64_t vbSize = asset.Vertices.size() * sizeof(Vertex);
    uint64_t ibSize = asset.Indices.size() * sizeof(uint32_t);

    CreateBuffer(gpu.VertexBuffer.GetAddressOf(), asset.Vertices.data(), vbSize);
    CreateBuffer(gpu.IndexBuffer.GetAddressOf(), asset.Indices.data(), ibSize);

    gpu.VBV = { gpu.VertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(vbSize), sizeof(Vertex) };
    gpu.IBV = { gpu.IndexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(ibSize), DXGI_FORMAT_R32_UINT };
    gpu.VertexCount = static_cast<uint32_t>(asset.Vertices.size());
    gpu.IndexCount = static_cast<uint32_t>(asset.Indices.size());

    return gpu;
}

static uint32_t ToSceneHeapIndex(TextureID textureId, TextureManager& textureManager)
{
    if (textureId == INVALID_TEXTURE)
        return Graphics::g_BindlessHeap.GetOffsetOfHandle(Renderer::g_NullDescriptor);

    return textureManager.GetBindlessIndex(textureId);
}

static TextureID LoadTinyGltfImage(const tinygltf::Image& img, TextureManager& textureManager, bool srgb)
{
    if (img.image.empty() || img.width <= 0 || img.height <= 0)
        return INVALID_TEXTURE;

    if (img.as_is)
        return textureManager.LoadTextureFromMemory(img.image.data(), img.image.size(), srgb);

    if (img.bits != 8 || img.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
        return INVALID_TEXTURE;

    const size_t pixelCount = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    std::vector<uint8_t> rgba(pixelCount * 4);

    switch (img.component)
    {
    case 1:
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const uint8_t v = img.image[i];
            rgba[i * 4 + 0] = v;
            rgba[i * 4 + 1] = v;
            rgba[i * 4 + 2] = v;
            rgba[i * 4 + 3] = 255;
        }
        break;
    case 2:
        for (size_t i = 0; i < pixelCount; ++i)
        {
            rgba[i * 4 + 0] = img.image[i * 2 + 0];
            rgba[i * 4 + 1] = img.image[i * 2 + 0];
            rgba[i * 4 + 2] = img.image[i * 2 + 0];
            rgba[i * 4 + 3] = img.image[i * 2 + 1];
        }
        break;
    case 3:
        for (size_t i = 0; i < pixelCount; ++i)
        {
            rgba[i * 4 + 0] = img.image[i * 3 + 0];
            rgba[i * 4 + 1] = img.image[i * 3 + 1];
            rgba[i * 4 + 2] = img.image[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
        break;
    case 4:
        memcpy(rgba.data(), img.image.data(), rgba.size());
        break;
    default:
        return INVALID_TEXTURE;
    }

    return textureManager.CreateTexture2D(
        static_cast<uint32_t>(img.width),
        static_cast<uint32_t>(img.height),
        srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,
        rgba.data());
}

static void LoadModelFromGLTF(const std::string& path, ModelAsset& asset, ModelGPU& gpu, TextureManager& textureManager)
{
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;

    std::string err;
    std::string warn;

    bool ret = path.ends_with(".glb")
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
        : loader.LoadASCIIFromFile(&model, &err, &warn, path);

    assert(ret);

    std::string baseDir = path.substr(0, path.find_last_of("\\/") + 1);

    asset.Materials.resize(model.materials.size());
    if (asset.Materials.empty())
        asset.Materials.emplace_back();

    for (size_t i = 0; i < model.materials.size(); i++)
    {
        const auto& mat = model.materials[i];
        auto& out = asset.Materials[i];

        auto loadTex = [&](int texIndex, bool srgb)
        {
            if (texIndex < 0 || texIndex >= static_cast<int>(model.textures.size()))
                return INVALID_TEXTURE;

            const auto& tex = model.textures[texIndex];
            if (tex.source < 0 || tex.source >= static_cast<int>(model.images.size()))
                return INVALID_TEXTURE;
            const auto& img = model.images[tex.source];

            if (!img.image.empty())
                return LoadTinyGltfImage(img, textureManager, srgb);

            if (!img.uri.empty())
            {
                std::filesystem::path fullPath = std::filesystem::path(baseDir) / std::filesystem::path(img.uri);
                return textureManager.LoadTexture(fullPath.generic_wstring(), srgb);
            }

            return INVALID_TEXTURE;
        };

        out.BaseColor = loadTex(mat.pbrMetallicRoughness.baseColorTexture.index, true);
        out.Normal = loadTex(mat.normalTexture.index, false);
        out.RoughnessMetallic = loadTex(mat.pbrMetallicRoughness.metallicRoughnessTexture.index, false);
        out.Emissive = loadTex(mat.emissiveTexture.index, true);
    }

    for (const auto& mesh : model.meshes)
    {
        MeshAsset meshAsset{};
        meshAsset.Name = mesh.name;
        bool needsNormalRebuild = false;
        bool needsTangentRebuild = false;

        for (const auto& primitive : mesh.primitives)
            LoadPrimitive(model, primitive, meshAsset, needsNormalRebuild, needsTangentRebuild);

        // Rebuild once after all primitives are merged into one mesh.
        RebuildVertexNormalsAndTangents(meshAsset, needsNormalRebuild, needsTangentRebuild);
        asset.Meshes.push_back(std::move(meshAsset));
    }

    for (auto& m : asset.Meshes)
        gpu.Meshes.push_back(UploadMesh(m));

    gpu.Materials.resize(asset.Materials.size());

    for (size_t i = 0; i < asset.Materials.size(); i++)
    {
        const auto& m = asset.Materials[i];
        auto& g = gpu.Materials[i];
        g.BaseColorIndex = ToSceneHeapIndex(m.BaseColor, textureManager);
        g.NormalIndex = ToSceneHeapIndex(m.Normal, textureManager);
        g.RoughnessMetallicIndex = ToSceneHeapIndex(m.RoughnessMetallic, textureManager);
        g.EmissiveIndex = ToSceneHeapIndex(m.Emissive, textureManager);
    }
}

void Model::Draw(GraphicsContext& context, bool bindMaterialData)
{
    for (size_t i = 0; i < GPU.Meshes.size(); i++)
    {
        const auto& mesh = GPU.Meshes[i];
        const auto& meshAsset = Asset->Meshes[i];

        context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context.GetCommandList()->IASetVertexBuffers(0, 1, &mesh.VBV);
        context.GetCommandList()->IASetIndexBuffer(&mesh.IBV);

        for (const auto& sub : meshAsset.SubMeshes)
        {
            const size_t materialIndex = sub.MaterialIndex < GPU.Materials.size() ? sub.MaterialIndex : 0;
            const MaterialGPU& mat = GPU.Materials[materialIndex];

            if (bindMaterialData)
            {
                MaterialConstants materialConstants{};
                materialConstants.BaseColorIndex = mat.BaseColorIndex;
                materialConstants.NormalIndex = mat.NormalIndex;
                materialConstants.RoughnessMetallicIndex = mat.RoughnessMetallicIndex;
                materialConstants.EmissiveIndex = mat.EmissiveIndex;
                context.SetDynamicConstantBufferView(Renderer::kMaterialConstants, sizeof(MaterialConstants), &materialConstants);
            }

            context.GetCommandList()->DrawIndexedInstanced(sub.IndexCount, 1, sub.IndexOffset, 0, 0);
        }
    }
}
namespace ModelUtility
{
    LoadedModelData LoadGltfAsset(const std::string& path)
    {
        LoadedModelData data;
        data.Asset = std::make_shared<ModelAsset>();
        LoadModelFromGLTF(path, *data.Asset, data.GPU, Graphics::g_TextureManager);
        return data;
    }

    Model CreateModel(LoadedModelData&& data)
    {
        Model model;
        model.Asset = std::move(data.Asset);
        model.GPU = std::move(data.GPU);
        model.UpdateConstants();
        return model;
    }

}
