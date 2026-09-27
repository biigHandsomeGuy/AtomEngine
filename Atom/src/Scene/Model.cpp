#include "pch.h"
#include "Model.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cctype>

#include "GraphicsCore.h"
#include "CommandContext.h"
#include "Renderer.h"
#include "ConstantBuffers.h"
#include "PathUtils.h"

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

static void RebuildVertexNormalsAndTangents(MeshAsset& mesh, bool rebuildNormals, bool rebuildTangents, const std::atomic_bool* cancelled)
{
    if (mesh.Vertices.empty() || mesh.Indices.size() < 3)
        return;

    std::vector<Vector3> normalAccum(mesh.Vertices.size(), Vector3(kZero));
    std::vector<Vector3> tangentAccum(mesh.Vertices.size(), Vector3(kZero));
    std::vector<Vector3> bitangentAccum(mesh.Vertices.size(), Vector3(kZero));

    for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
    {
        if ((i & 4095) == 0 && cancelled && cancelled->load()) throw std::runtime_error("Import cancelled.");
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
        if ((i & 4095) == 0 && cancelled && cancelled->load()) throw std::runtime_error("Import cancelled.");
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

static void ValidateAccessor(const tinygltf::Model& model, int index, int type, bool floatingPoint)
{
    const auto& accessor = model.accessors.at(index);
    if (accessor.type != type || accessor.sparse.isSparse || accessor.bufferView < 0 || accessor.count == 0)
        throw std::runtime_error("Unsupported or empty glTF accessor (sparse accessors are not supported).");
    const auto& view = model.bufferViews.at(accessor.bufferView);
    const auto& buffer = model.buffers.at(view.buffer);
    if (floatingPoint && accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
        throw std::runtime_error("Vertex attributes must use floating-point components.");
    if (!floatingPoint && accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
        accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT && accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
        throw std::runtime_error("Indices must use unsigned integer components.");
    const int componentBytes = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    const int components = tinygltf::GetNumComponentsInType(accessor.type);
    const int stride = accessor.ByteStride(view);
    const size_t elementBytes = static_cast<size_t>(componentBytes) * components;
    if (stride < 0 || size_t(stride) < elementBytes || view.byteOffset > buffer.data.size() ||
        view.byteLength > buffer.data.size() - view.byteOffset || accessor.byteOffset > view.byteLength ||
        elementBytes > view.byteLength - accessor.byteOffset ||
        accessor.count - 1 > (view.byteLength - accessor.byteOffset - elementBytes) / size_t(stride))
        throw std::runtime_error("glTF accessor exceeds its buffer view.");
}

static void LoadPrimitive(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    MeshAsset& mesh,
    bool& needsNormalRebuild,
    bool& needsTangentRebuild,
    const std::atomic_bool* cancelled)
{
    if (primitive.mode != -1 && primitive.mode != TINYGLTF_MODE_TRIANGLES)
        throw std::runtime_error("Only triangle primitives are currently supported.");
    ValidateAccessor(model, primitive.attributes.at("POSITION"), TINYGLTF_TYPE_VEC3, true);
    const auto& posAcc = model.accessors[primitive.attributes.at("POSITION")];
    for (const auto& attribute : { "NORMAL", "TEXCOORD_0", "TANGENT" })
    {
        if (!primitive.attributes.contains(attribute)) continue;
        const int index = primitive.attributes.at(attribute);
        ValidateAccessor(model, index, std::string(attribute) == "TEXCOORD_0" ? TINYGLTF_TYPE_VEC2 :
            std::string(attribute) == "TANGENT" ? TINYGLTF_TYPE_VEC4 : TINYGLTF_TYPE_VEC3, true);
        if (model.accessors.at(index).count != posAcc.count)
            throw std::runtime_error("Vertex attribute counts do not match POSITION.");
    }
    if (posAcc.count > UINT32_MAX / sizeof(Vertex) || mesh.Vertices.size() > UINT32_MAX / sizeof(Vertex) - posAcc.count)
        throw std::runtime_error("Mesh vertex buffer exceeds the supported size.");
    if (primitive.material < -1 || primitive.material >= static_cast<int>(model.materials.size()))
        throw std::runtime_error("Invalid glTF material index.");
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
        if ((i & 4095) == 0 && cancelled && cancelled->load()) throw std::runtime_error("Import cancelled.");
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
        ValidateAccessor(model, primitive.indices, TINYGLTF_TYPE_SCALAR, false);
        const auto& idxAcc = model.accessors[primitive.indices];
        const auto& view = model.bufferViews[idxAcc.bufferView];
        const auto& buffer = model.buffers[view.buffer];
        const uint8_t* data = buffer.data.data() + view.byteOffset + idxAcc.byteOffset;
        primitiveIndexCount = static_cast<uint32_t>(idxAcc.count);
        if (idxAcc.count > UINT32_MAX / sizeof(uint32_t) || mesh.Indices.size() > UINT32_MAX / sizeof(uint32_t) - idxAcc.count)
            throw std::runtime_error("Mesh index buffer exceeds the supported size.");

        for (size_t i = 0; i < idxAcc.count; i++)
        {
            if ((i & 4095) == 0 && cancelled && cancelled->load()) throw std::runtime_error("Import cancelled.");
            uint32_t index = 0;

            switch (idxAcc.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                index = *reinterpret_cast<const uint16_t*>(data + i * idxAcc.ByteStride(view));
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                index = *reinterpret_cast<const uint32_t*>(data + i * idxAcc.ByteStride(view));
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                index = data[i * idxAcc.ByteStride(view)];
                break;
            default:
                break;
            }

            if (index >= vertexCount)
                throw std::runtime_error("glTF index exceeds its primitive vertex count.");
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

    if (primitiveIndexCount == 0 || primitiveIndexCount % 3 != 0)
        throw std::runtime_error("Triangle primitive has an invalid index count.");

    SubMesh sub{};
    sub.IndexOffset = indexBase;
    sub.IndexCount = primitiveIndexCount;
    sub.MaterialIndex = primitive.material >= 0 ? static_cast<uint32_t>(primitive.material) : 0;
    mesh.SubMeshes.push_back(sub);
}

static void CreateBuffer(ID3D12Resource** resource, const void* data, uint64_t size, CommandContext& context)
{
    if (size == 0 || size > UINT32_MAX)
        throw std::runtime_error("A mesh buffer is empty or exceeds the supported size.");
    ASSERT_SUCCEEDED(Graphics::g_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(size), D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(resource)));
    DynAlloc upload = context.ReserveUploadMemory(size);
    memcpy(upload.DataPtr, data, static_cast<size_t>(size));
    context.GetCommandList()->CopyBufferRegion(*resource, 0, upload.Buffer.GetResource(), upload.Offset, size);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(*resource,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
    context.GetCommandList()->ResourceBarrier(1, &barrier);
}

static MeshGPU UploadMesh(const MeshAsset& asset, CommandContext& context)
{
    MeshGPU gpu{};
    const uint64_t vbSize = asset.Vertices.size() * sizeof(Vertex);
    const uint64_t ibSize = asset.Indices.size() * sizeof(uint32_t);
    CreateBuffer(gpu.VertexBuffer.GetAddressOf(), asset.Vertices.data(), vbSize, context);
    CreateBuffer(gpu.IndexBuffer.GetAddressOf(), asset.Indices.data(), ibSize, context);
    gpu.VBV = { gpu.VertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(vbSize), sizeof(Vertex) };
    gpu.IBV = { gpu.IndexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(ibSize), DXGI_FORMAT_R32_UINT };
    gpu.VertexCount = static_cast<uint32_t>(asset.Vertices.size());
    gpu.IndexCount = static_cast<uint32_t>(asset.Indices.size());
    return gpu;
}
static uint32_t ToSceneHeapIndex(TextureID textureId, TextureManager& textureManager)
{
    if (textureId == INVALID_TEXTURE)
        return textureManager.GetNullBindlessIndex();

    return textureManager.GetBindlessIndex(textureId);
}

static ImageAsset DecodeImage(const tinygltf::Image& img, bool srgb)
{
    if (img.image.empty() || img.width <= 0 || img.height <= 0)
        throw std::runtime_error("Invalid glTF image dimensions or components.");

    if (img.as_is || img.bits != 8 || img.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
        throw std::runtime_error("Only decoded 8-bit glTF images are supported.");

    const size_t pixelCount = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    if (img.width > 16384 || img.height > 16384 || img.component < 1 || img.component > 4 || img.image.size() < pixelCount * img.component)
        throw std::runtime_error("Malformed or oversized glTF image.");
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
        throw std::runtime_error("Invalid glTF image dimensions or components.");
    }

    return { static_cast<uint32_t>(img.width), static_cast<uint32_t>(img.height), srgb, std::move(rgba) };
}

static bool DecodeModelFromGLTF(const std::string& path, ModelAsset& asset, std::string& error, std::string* warnings, const std::atomic_bool* cancelled)
{
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;

    std::string err;
    std::string warn;

    std::string extension = PathFromUtf8(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    bool ret = extension == ".glb"
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
        : loader.LoadASCIIFromFile(&model, &err, &warn, path);

    if (!ret)
    {
        error = err.empty() ? "The glTF loader rejected the file." : err;
        if (!warn.empty())
            error += "\n" + warn;
        return false;
    }

    if (model.meshes.empty())
    {
        error = "The glTF file does not contain any meshes.";
        return false;
    }

    if (warnings) *warnings = warn;
    std::map<std::pair<int, bool>, uint32_t> decodedImages;

    asset.Materials.resize(model.materials.size());
    if (asset.Materials.empty())
        asset.Materials.emplace_back();

    for (size_t i = 0; i < model.materials.size(); i++)
    {
        const auto& mat = model.materials[i];
        auto& out = asset.Materials[i];

        if (cancelled && cancelled->load())
            throw std::runtime_error("Import cancelled.");
        auto loadTex = [&](int texIndex, bool srgb) -> uint32_t
        {
            if (texIndex < 0) return INVALID_INDEX;
            const auto& tex = model.textures.at(texIndex);
            if (tex.source < 0) return INVALID_INDEX;
            const auto key = std::make_pair(tex.source, srgb);
            if (auto found = decodedImages.find(key); found != decodedImages.end()) return found->second;
            ImageAsset image = DecodeImage(model.images.at(tex.source), srgb);
            const uint32_t index = static_cast<uint32_t>(asset.Images.size());
            asset.Images.push_back(std::move(image));
            decodedImages.emplace(key, index);
            return index;
        };
        out.BaseColor = loadTex(mat.pbrMetallicRoughness.baseColorTexture.index, true);
        out.Normal = loadTex(mat.normalTexture.index, false);
        out.RoughnessMetallic = loadTex(mat.pbrMetallicRoughness.metallicRoughnessTexture.index, false);
        out.Emissive = loadTex(mat.emissiveTexture.index, true);
        if (mat.emissiveFactor.size() == 3)
            out.EmissiveFactor = Vector3(float(mat.emissiveFactor[0]), float(mat.emissiveFactor[1]), float(mat.emissiveFactor[2]));
    }

    for (const auto& mesh : model.meshes)
    {
        if (mesh.primitives.empty()) throw std::runtime_error("glTF mesh contains no primitives.");
        if (cancelled && cancelled->load()) throw std::runtime_error("Import cancelled.");
        MeshAsset meshAsset{};
        meshAsset.Name = mesh.name;
        bool needsNormalRebuild = false;
        bool needsTangentRebuild = false;

        for (const auto& primitive : mesh.primitives)
            LoadPrimitive(model, primitive, meshAsset, needsNormalRebuild, needsTangentRebuild, cancelled);

        // Rebuild once after all primitives are merged into one mesh.
        RebuildVertexNormalsAndTangents(meshAsset, needsNormalRebuild, needsTangentRebuild, cancelled);
        asset.Meshes.push_back(std::move(meshAsset));
    }

    return true;
}

void Model::Draw(GraphicsContext& context, bool bindMaterialData)
{
    if (!Asset || !GPU)
        return;

    for (size_t i = 0; i < GPU->Meshes.size(); i++)
    {
        const auto& mesh = GPU->Meshes[i];
        const auto& meshAsset = Asset->Meshes[i];

        context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context.GetCommandList()->IASetVertexBuffers(0, 1, &mesh.VBV);
        context.GetCommandList()->IASetIndexBuffer(&mesh.IBV);

        for (const auto& sub : meshAsset.SubMeshes)
        {
            const size_t materialIndex = sub.MaterialIndex < GPU->Materials.size() ? sub.MaterialIndex : 0;
            const MaterialGPU& mat = GPU->Materials[materialIndex];

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
    std::shared_ptr<const ModelAsset> DecodeGltfAsset(const std::string& path, std::string* error,
        std::string* warnings, const std::atomic_bool* cancelled)
    {
        if (error) error->clear();
        if (warnings) warnings->clear();
        auto asset = std::make_shared<ModelAsset>();
        try
        {
            if (cancelled && cancelled->load()) throw std::runtime_error("Import cancelled.");
            asset->Name = PathToUtf8(PathFromUtf8(path).stem());
            asset->SourcePath = PathToUtf8(PathFromUtf8(path).lexically_normal());
            std::string message;
            if (!DecodeModelFromGLTF(path, *asset, message, warnings, cancelled))
                throw std::runtime_error(message);
            if (cancelled && cancelled->load()) throw std::runtime_error("Import cancelled.");
            return asset;
        }
        catch (const std::exception& exception)
        {
            if (error) *error = exception.what();
            return {};
        }
    }

    std::shared_ptr<const ModelGPU> UploadAsset(const ModelAsset& asset, CommandContext& context)
    {
        auto gpu = std::make_shared<ModelGPU>();
        for (const auto& image : asset.Images)
            gpu->Textures.push_back(Graphics::g_TextureManager.CreateTexture2D(image.Width, image.Height,
                image.Srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,
                image.Pixels.data(), &context));
        for (const auto& mesh : asset.Meshes)
            gpu->Meshes.push_back(UploadMesh(mesh, context));
        auto resolve = [&](uint32_t index)
        {
            return ToSceneHeapIndex(index == INVALID_INDEX ? INVALID_TEXTURE : gpu->Textures.at(index), Graphics::g_TextureManager);
        };
        for (const auto& material : asset.Materials)
        {
            MaterialGPU result{};
            result.BaseColorIndex = resolve(material.BaseColor);
            result.NormalIndex = resolve(material.Normal);
            result.RoughnessMetallicIndex = resolve(material.RoughnessMetallic);
            result.EmissiveIndex = resolve(material.Emissive);
            result.EmissiveFactor = material.EmissiveFactor;
            gpu->Materials.push_back(result);
        }
        return gpu;
    }

    LoadedModelData LoadGltfAsset(const std::string& path, std::string* error)
    {
        LoadedModelData data;
        data.Asset = DecodeGltfAsset(path, error);
        if (!data.Asset) return data;
        auto& context = CommandContext::Begin(L"Model asset upload");
        data.GPU = UploadAsset(*data.Asset, context);
        context.Finish(true);
        return data;
    }
    Model CreateModel(LoadedModelData&& data)
    {
        Model model;
        model.Asset = std::move(data.Asset);
        model.GPU = std::move(data.GPU);
        if (model.Asset)
            model.InstanceName = model.Asset->Name;
        model.UpdateConstants();
        return model;
    }

}
