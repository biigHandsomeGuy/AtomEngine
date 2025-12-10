// GltfLoader.cpp
#include "pch.h"
#include "Model.h"
#include "GraphicsCore.h"
#include "Renderer.h"
#include "FileSystem.h"
#include "TextureManager.h"
#include "tiny_gltf.h"

// stb_image for decoding PNG/JPEG from base64 or compressed image buffers
#include "stb_image.h"


// -------------------- Helpers --------------------

static bool EndsWith(const std::string& s, const std::string& suffix)
{
    if (s.size() < suffix.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Read buffer pointer + stride for accessor
static const unsigned char* GetBufferPtr(const tinygltf::Model& gltf, const tinygltf::Accessor& acc, int& outByteStride)
{
    if (acc.bufferView < 0 || acc.bufferView >= (int)gltf.bufferViews.size()) {
        outByteStride = 0;
        return nullptr;
    }
    const tinygltf::BufferView& bv = gltf.bufferViews[acc.bufferView];
    const tinygltf::Buffer& buf = gltf.buffers[bv.buffer];
    const unsigned char* base = buf.data.data() + bv.byteOffset + acc.byteOffset;

    if (bv.byteStride != 0)
        outByteStride = static_cast<int>(bv.byteStride);
    else {
        // compute element size by type and component (assume floats for positions/normals/uvs common case)
        int componentSize = 0;
        switch (acc.componentType) {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: componentSize = 1; break;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: componentSize = 2; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: componentSize = 4; break;
        case TINYGLTF_COMPONENT_TYPE_FLOAT: componentSize = 4; break;
        default: componentSize = 1; break;
        }

        int numComponents = 1;
        if (acc.type == TINYGLTF_TYPE_SCALAR) numComponents = 1;
        else if (acc.type == TINYGLTF_TYPE_VEC2) numComponents = 2;
        else if (acc.type == TINYGLTF_TYPE_VEC3) numComponents = 3;
        else if (acc.type == TINYGLTF_TYPE_VEC4) numComponents = 4;
        else if (acc.type == TINYGLTF_TYPE_MAT2) numComponents = 4;
        else if (acc.type == TINYGLTF_TYPE_MAT3) numComponents = 9;
        else if (acc.type == TINYGLTF_TYPE_MAT4) numComponents = 16;

        outByteStride = componentSize * numComponents;
    }

    return base;
}

// Read float vector from raw pointer (assume contiguous floats)
static void ReadFloatElement(const unsigned char* ptr, int compCount, Vector4& out)
{
    out = Vector4(0.0f);
    if (!ptr) return;
    const float* f = reinterpret_cast<const float*>(ptr);
    if (compCount >= 1) out.SetX(f[0]);
    if (compCount >= 2) out.SetY(f[1]);
    if (compCount >= 3) out.SetZ(f[2]);
    if (compCount >= 4) out.SetW(f[3]);
}

static uint32_t ReadIndexValue(const unsigned char* src, int componentType)
{
    switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return static_cast<uint32_t>(*reinterpret_cast<const uint16_t*>(src));
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        return static_cast<uint32_t>(*reinterpret_cast<const uint32_t*>(src));
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return static_cast<uint32_t>(*reinterpret_cast<const uint8_t*>(src));
    default:
        return 0;
    }
}

// Base64 decode (simple)
static inline unsigned char DecodeBase64Char(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return 255;
}

static std::vector<unsigned char> Base64Decode(const std::string& input)
{
    std::vector<unsigned char> output;
    output.reserve(input.size() * 3 / 4);

    unsigned int buffer = 0;
    int bitsLeft = 0;

    for (unsigned char c : input)
    {
        if (c == '=') break;
        unsigned char decoded = DecodeBase64Char(c);
        if (decoded == 255) continue;

        buffer = (buffer << 6) | decoded;
        bitsLeft += 6;

        if (bitsLeft >= 8)
        {
            bitsLeft -= 8;
            output.push_back(static_cast<unsigned char>((buffer >> bitsLeft) & 0xFF));
        }
    }

    return output;
}

// -------------------- Texture loading --------------------
// Forward declare functions from your TextureManager
// Expected: TextureRef LoadTexFromFile(const std::wstring& path);
//           TextureRef LoadTexFromMemory(const unsigned char* pixels, int w, int h);

// Load tinygltf::Image into TextureRef (handles external URI, data:base64, bufferView/raw)
TextureRef LoadGltfImageToTextureRef(const tinygltf::Image& image, const std::string& baseDir)
{
    // Case 1: external file (uri not empty and not data:)
    if (!image.uri.empty() && image.uri.rfind("data:", 0) != 0)
    {
        std::string fullPath = baseDir.empty() ? image.uri : (baseDir + "/" + image.uri);
        std::wstring wpath(fullPath.begin(), fullPath.end());
        return TextureManager::LoadTexFromFile(wpath);
    }

    // Case 2: data: URI (base64 compressed image)
    if (!image.uri.empty() && image.uri.rfind("data:", 0) == 0)
    {
        // find base64 part
        size_t pos = image.uri.find("base64,");
        if (pos == std::string::npos) {
            return TextureRef(nullptr);
        }
        std::string base64Data = image.uri.substr(pos + 7);
        std::vector<unsigned char> decoded = Base64Decode(base64Data);

        // decode compressed image (PNG/JPEG) to RGBA using stb_image
        int w = 0, h = 0, comp = 0;
        unsigned char* rgba = stbi_load_from_memory(decoded.data(), static_cast<int>(decoded.size()), &w, &h, &comp, 4);
        if (!rgba) {
            // failed to decode
            return TextureRef(nullptr);
        }

        TextureRef ref = TextureManager::LoadTexFromMemory(rgba, w, h);

        stbi_image_free(rgba);
        return ref;
    }

    // Case 3: image.image may contain decoded pixels OR compressed bytes depending on tinygltf settings
    if (!image.image.empty()) {
        // tinygltf often decodes PNG/JPG into image.image only if Load... did decode them
        // but to be robust: attempt to detect if image.image is already raw RGBA or compressed (PNG/JPG).
        // Heuristic: if image.component is set and equals channels (1/3/4) -> raw pixels
        if (image.component > 0) {
            // component = number of channels (e.g. 4 for RGBA) => treat as raw pixels
            // tinygltf stores raw pixel bytes in image.image when it decoded them.
            return TextureManager::LoadTexFromMemory(const_cast<unsigned char*>(image.image.data()), image.width, image.height);
        }
        else {
            // component == 0 -> image.image probably contains compressed file bytes (PNG/JPG)
            int w = 0, h = 0, comp = 0;
            unsigned char* rgba = stbi_load_from_memory(image.image.data(), static_cast<int>(image.image.size()), &w, &h, &comp, 4);
            if (!rgba) return TextureRef(nullptr);
            TextureRef ref = TextureManager::LoadTexFromMemory(rgba, w, h);
            stbi_image_free(rgba);
            return ref;
        }
    }

    return TextureRef(nullptr);
}

// -------------------- Material conversion --------------------

Material ConvertMaterial(const tinygltf::Model& gltf, const tinygltf::Material& gm, const std::string& baseDir)
{
    Material mat;

    // Base color factor
    if (gm.pbrMetallicRoughness.baseColorFactor.size() == 4) {
        mat.BaseColorFactor = Vector4(
            (float)gm.pbrMetallicRoughness.baseColorFactor[0],
            (float)gm.pbrMetallicRoughness.baseColorFactor[1],
            (float)gm.pbrMetallicRoughness.baseColorFactor[2],
            (float)gm.pbrMetallicRoughness.baseColorFactor[3]);
    }

    mat.MetallicFactor = (float)gm.pbrMetallicRoughness.metallicFactor;
    mat.RoughnessFactor = (float)gm.pbrMetallicRoughness.roughnessFactor;

    if (gm.emissiveFactor.size() == 3) {
        mat.EmissiveFactor = Vector3((float)gm.emissiveFactor[0], (float)gm.emissiveFactor[1], (float)gm.emissiveFactor[2]);
    }

    mat.DoubleSided = gm.doubleSided;
    mat.Name = gm.name;

    // helper to safely get texture -> image
    auto GetImageFromTextureIndex = [&](int texIdx) -> const tinygltf::Image* {
        if (texIdx < 0 || texIdx >= (int)gltf.textures.size()) return nullptr;
        const tinygltf::Texture& t = gltf.textures[texIdx];
        if (t.source < 0 || t.source >= (int)gltf.images.size()) return nullptr;
        return &gltf.images[t.source];
        };

    // Albedo
    if (gm.pbrMetallicRoughness.baseColorTexture.index >= 0) {
        if (const tinygltf::Image* img = GetImageFromTextureIndex(gm.pbrMetallicRoughness.baseColorTexture.index))
            mat.Albedo = LoadGltfImageToTextureRef(*img, baseDir);
    }

    // MetallicRoughness (single texture: B=metallic, G=roughness)
    if (gm.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
        if (const tinygltf::Image* img = GetImageFromTextureIndex(gm.pbrMetallicRoughness.metallicRoughnessTexture.index)) {
            TextureRef mr = LoadGltfImageToTextureRef(*img, baseDir);
            mat.Metallic = mr;
            mat.Roughness = mr;
        }
    }

    // Normal
    if (gm.normalTexture.index >= 0) {
        if (const tinygltf::Image* img = GetImageFromTextureIndex(gm.normalTexture.index))
            mat.Normal = LoadGltfImageToTextureRef(*img, baseDir);
    }

    // Occlusion
    if (gm.occlusionTexture.index >= 0) {
        if (const tinygltf::Image* img = GetImageFromTextureIndex(gm.occlusionTexture.index))
            mat.Occlusion = LoadGltfImageToTextureRef(*img, baseDir);
    }

    // Emissive
    if (gm.emissiveTexture.index >= 0) {
        if (const tinygltf::Image* img = GetImageFromTextureIndex(gm.emissiveTexture.index))
            mat.Emissive = LoadGltfImageToTextureRef(*img, baseDir);
    }

    return mat;
}

// -------------------- Mesh conversion --------------------

Mesh ConvertMesh(const tinygltf::Model& gltf, const tinygltf::Mesh& gmesh, const std::string& baseDir)
{
    Mesh mesh;
    mesh.Name = gmesh.name;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // iterate primitives: each primitive -> one Submesh
    for (const tinygltf::Primitive& prim : gmesh.primitives) {
        // record base offsets
        uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
        uint32_t baseIndex = static_cast<uint32_t>(indices.size());

        // ---- determine vertex count by reading POSITION accessor.count
        int posAccessorIndex = -1;
        auto posIt = prim.attributes.find("POSITION");
        if (posIt != prim.attributes.end()) posAccessorIndex = posIt->second;
        if (posAccessorIndex < 0) continue;
        const tinygltf::Accessor& posAcc = gltf.accessors[posAccessorIndex];
        size_t primVertexCount = posAcc.count;

        // resize vertex array to hold this primitive
        vertices.resize(baseVertex + primVertexCount);

        // read POSITION
        {
            int stride = 0;
            const unsigned char* ptr = GetBufferPtr(gltf, posAcc, stride);
            for (size_t i = 0; i < posAcc.count; ++i) {
                const unsigned char* elem = ptr + i * stride;
                Vector4 p4; ReadFloatElement(elem, 3, p4);
                vertices[baseVertex + i].Position = Vector3(p4.GetX(), p4.GetY(), p4.GetZ());
            }
        }

        // Optional attributes: NORMAL, TEXCOORD_0, TANGENT
        auto readOptional = [&](const char* semantic, auto assignFunc) {
            auto it = prim.attributes.find(semantic);
            if (it == prim.attributes.end()) return;
            const tinygltf::Accessor& acc = gltf.accessors[it->second];
            int stride = 0;
            const unsigned char* ptr = GetBufferPtr(gltf, acc, stride);
            int compCount = (acc.type == TINYGLTF_TYPE_VEC2) ? 2 : (acc.type == TINYGLTF_TYPE_VEC3 ? 3 : 4);
            for (size_t i = 0; i < acc.count; ++i) {
                const unsigned char* elem = ptr + i * stride;
                Vector4 tmp; ReadFloatElement(elem, compCount, tmp);
                assignFunc(baseVertex + i, tmp);
            }
            };

        readOptional("NORMAL", [&](size_t idx, const Vector4& v) { vertices[idx].Normal = Vector3(v.GetX(), v.GetY(), v.GetZ()); });
        readOptional("TEXCOORD_0", [&](size_t idx, const Vector4& v) { vertices[idx].UV = XMFLOAT2(v.GetX(), v.GetY()); });
        readOptional("TANGENT", [&](size_t idx, const Vector4& v) { vertices[idx].Tangent = Vector3(v.GetX(), v.GetY(), v.GetZ()); /* w (handedness) ignored here */ });
        readOptional("BITANGENT", [&](size_t idx, const Vector4& v) { vertices[idx].BiTangent = Vector3(v.GetX(), v.GetY(), v.GetZ()); /* w (handedness) ignored here */ });

        // ---- indices
        if (prim.indices >= 0) {
            const tinygltf::Accessor& idxAcc = gltf.accessors[prim.indices];
            int idxStride = 0;
            const unsigned char* idxBase = GetBufferPtr(gltf, idxAcc, idxStride);
            for (size_t k = 0; k < idxAcc.count; ++k) {
                const unsigned char* src = idxBase + k * idxStride;
                uint32_t raw = ReadIndexValue(src, idxAcc.componentType);
                indices.push_back(raw + baseVertex); // offset by baseVertex
            }
        }
        else {
            // no indices -> generate sequential indices (triangle list assumption)
            for (uint32_t v = 0; v < primVertexCount; ++v) {
                indices.push_back(baseVertex + v);
            }
        }

        // ---- build submesh
        Submesh sub;
        sub.StartIndex = baseIndex;
        sub.IndexCount = static_cast<uint32_t>(indices.size()) - baseIndex;
        sub.BaseVertex = baseVertex;
        sub.Bounds = DirectX::BoundingBox(); // optional: compute properly later

        if (prim.material >= 0 && prim.material < (int)gltf.materials.size()) {
            sub.Material = ConvertMaterial(gltf, gltf.materials[prim.material], baseDir);
        }
        else {
            sub.Material = Material(); // default
        }

        mesh.Submeshes.push_back(std::move(sub));
    }

    // attach CPU-side arrays
    mesh.CPUVertices = std::move(vertices);
    mesh.CPUIndices = std::move(indices);

    return mesh;
}

// -------------------- GPU upload (UPLOAD heap, Map/Unmap) --------------------

void UploadMeshToGPU(Mesh& mesh)
{
    ID3D12Device* device = Graphics::g_Device;

    mesh.VertexCount = static_cast<uint32_t>(mesh.CPUVertices.size());
    mesh.IndexCount = static_cast<uint32_t>(mesh.CPUIndices.size());

    const size_t vbSize = mesh.VertexCount * sizeof(Vertex);
    const size_t ibSize = mesh.IndexCount * sizeof(uint32_t);

    if (vbSize > 0) {
        ASSERT_SUCCEEDED(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vbSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mesh.VertexBuffer)));

        // map & copy
        {
            UINT8* mapped = nullptr;
            ASSERT_SUCCEEDED(mesh.VertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
            memcpy(mapped, mesh.CPUVertices.data(), vbSize);
            mesh.VertexBuffer->Unmap(0, nullptr);
        }

        mesh.VBV.BufferLocation = mesh.VertexBuffer->GetGPUVirtualAddress();
        mesh.VBV.SizeInBytes = static_cast<UINT>(vbSize);
        mesh.VBV.StrideInBytes = sizeof(Vertex);
    }

    if (ibSize > 0) {
        ASSERT_SUCCEEDED(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(ibSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mesh.IndexBuffer)));

        {
            UINT8* mapped = nullptr;
            ASSERT_SUCCEEDED(mesh.IndexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
            memcpy(mapped, mesh.CPUIndices.data(), ibSize);
            mesh.IndexBuffer->Unmap(0, nullptr);
        }

        mesh.IBV.BufferLocation = mesh.IndexBuffer->GetGPUVirtualAddress();
        mesh.IBV.SizeInBytes = static_cast<UINT>(ibSize);
        mesh.IBV.Format = DXGI_FORMAT_R32_UINT;
    }
}

// -------------------- Material SRV creation --------------------

void Model::CreateMaterialSRVs()
{
    using namespace Graphics;
    using namespace Renderer;

    MaterialSRVs.resize(Materials.size());

    for (size_t i = 0; i < Materials.size(); ++i)
    {
        Material& mat = Materials[i];

        std::vector<TextureRef*> texRefs;
        if (mat.Albedo.IsValid())    texRefs.push_back(&mat.Albedo);
        if (mat.Normal.IsValid())    texRefs.push_back(&mat.Normal);
        if (mat.Metallic.IsValid())  texRefs.push_back(&mat.Metallic);
        if (mat.Roughness.IsValid()) texRefs.push_back(&mat.Roughness);
        if (mat.Occlusion.IsValid()) texRefs.push_back(&mat.Occlusion);
        if (mat.Emissive.IsValid())  texRefs.push_back(&mat.Emissive);

        uint32_t texCount = static_cast<uint32_t>(texRefs.size());
        if (texCount == 0) {
            MaterialSRVs[i] = DescriptorHandle(); // empty
            continue;
        }

        MaterialSRVs[i] = Renderer::s_TextureHeap.Alloc(texCount);
        DescriptorHandle dst = MaterialSRVs[i];

        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> src;
        src.reserve(texCount);
        for (TextureRef* t : texRefs) src.push_back(t->GetSRV());

        Graphics::g_Device->CopyDescriptors(
            1,
            &dst,
            &texCount,
            texCount,
            src.data(),
            nullptr,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }
}

// -------------------- Top-level loader --------------------

Model LoadGltfModel(const std::string& path)
{
    tinygltf::Model gltf;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    std::string baseDir;
    size_t sep = path.find_last_of("/\\");
    if (sep != std::string::npos) baseDir = path.substr(0, sep);

    bool isGlb = EndsWith(path, ".glb") || EndsWith(path, ".GLB");
    bool ok = false;
    if (isGlb)
        ok = loader.LoadBinaryFromFile(&gltf, &err, &warn, path);
    else
        ok = loader.LoadASCIIFromFile(&gltf, &err, &warn, path);

    if (!warn.empty()) OutputDebugStringA(warn.c_str());
    if (!err.empty())  OutputDebugStringA(err.c_str());
    if (!ok) throw std::runtime_error("Failed to load glTF: " + path);

    Model model;
    model.Name = path;

    // Convert materials first (so textures are created and ready)
    model.Materials.clear();
    model.Materials.reserve(gltf.materials.size());
    for (const auto& gm : gltf.materials) {
        model.Materials.push_back(ConvertMaterial(gltf, gm, baseDir));
    }

    // Meshes
    model.Meshes.clear();
    for (const auto& gmesh : gltf.meshes) {
        Mesh m = ConvertMesh(gltf, gmesh, baseDir);
        UploadMeshToGPU(m);
        model.Meshes.push_back(std::move(m));
    }

    // Optional: create descriptor blocks for all materials
    model.CreateMaterialSRVs();

    return model;
}
void Model::Draw(ID3D12GraphicsCommandList* cmdList, bool isSkyBox)
{
    for (uint32_t meshIndex = 0; meshIndex < Meshes.size(); meshIndex++)
    {
        if(!isSkyBox)
        cmdList->SetGraphicsRootDescriptorTable(Renderer::kMaterialSRVs, MaterialSRVs[meshIndex]);


        cmdList->IASetVertexBuffers(0, 1, &Meshes[meshIndex].VBV);
        cmdList->IASetIndexBuffer(&Meshes[meshIndex].IBV);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->DrawIndexedInstanced(Meshes[meshIndex].CPUIndices.size(), 1, 0, 0, 0);
    }
}

