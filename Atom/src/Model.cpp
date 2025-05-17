
#include "pch.h"
#include "Model.h"
#include "GraphicsCore.h"
#include "Renderer.h"
#include "d3dUtil.h"

using namespace DirectX;
void ComputeTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
void Model::Load(const std::wstring& filepath, ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, Utility::WideStringToUTF8(filepath).c_str());

    if (!warn.empty()) {
        OutputDebugStringA(warn.c_str());
    }
    if (!err.empty()) {
        throw std::runtime_error("Failed to load model: " + err);
    }
    if (!ret) {
        throw std::runtime_error("Failed to load model: ");
    }

    meshes.clear();
    for (const auto& shape : shapes) {
        meshes.push_back(ProcessMesh(attrib, shape, device, commandList));
    }  

    LoadTextures(L"D:/AtomEngine/Atom/Assets/Textures/gold/");

}

void Model::Draw(ID3D12GraphicsCommandList* commandList)
{
    for (uint32_t meshIndex = 0; meshIndex < meshes.size(); meshIndex++)
    {
        commandList->SetGraphicsRootDescriptorTable(Renderer::kMaterialSRVs, m_SRVs);


        commandList->IASetVertexBuffers(0, 1, &meshes[meshIndex].vbv);
        commandList->IASetIndexBuffer(&meshes[meshIndex].ibv);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawIndexedInstanced(meshes[meshIndex].Indices.size(), 1, 0, 0, 0);
    }
}

void Model::LoadTextures(const std::wstring& basePath)
{
    using namespace Graphics;
    using namespace Renderer;
    using namespace TextureManager;

    m_TextureReferences.resize(4);
    m_SRVs = Renderer::s_TextureHeap.Alloc(4);
    m_SRVDescriptorSize = Renderer::s_TextureHeap.GetDescriptorSize();

    DescriptorHandle SRVs = m_SRVs;

    TextureRef* MatTextures = m_TextureReferences.data();
    for (uint32_t materialIdx = 0; materialIdx < 1; materialIdx++)
    {
        MatTextures[0] = LoadTexFromFile(basePath + L"albedo.png", kWhiteOpaque2D, true);

        MatTextures[1] = LoadTexFromFile(basePath + L"roughness.png", kWhiteOpaque2D, true);

        MatTextures[2] = LoadTexFromFile(basePath + L"metallic.png", kWhiteOpaque2D, true);

        MatTextures[3] = LoadTexFromFile(basePath + L"normal.png", kDefaultNormalMap, false);

        uint32_t DestCount = 4;
        uint32_t SourceCounts[] = { 1, 1, 1, 1};
        D3D12_CPU_DESCRIPTOR_HANDLE SourceTextures[4] =
        {
            MatTextures[0].GetSRV(),
            MatTextures[1].GetSRV(),
            MatTextures[2].GetSRV(),
            MatTextures[3].GetSRV(),
        };

        Graphics::g_Device->CopyDescriptors(1, &SRVs, &DestCount,
            DestCount, SourceTextures, SourceCounts, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        SRVs += (m_SRVDescriptorSize * 4);
        MatTextures += 4;
    }
}

Mesh Model::ProcessMesh(const tinyobj::attrib_t& attrib, const tinyobj::shape_t& shape, ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
{
    Mesh newMesh;

    // 提取顶点数据
    for (const auto& index : shape.mesh.indices) {
        Vertex vertex;

        // 位置
        vertex.Position = {
            attrib.vertices[3 * index.vertex_index + 0],
            attrib.vertices[3 * index.vertex_index + 1],
            attrib.vertices[3 * index.vertex_index + 2]
        };

        // 法线
        if (!attrib.normals.empty()) {
            vertex.Normal = {
                attrib.normals[3 * index.normal_index + 0],
                attrib.normals[3 * index.normal_index + 1],
                attrib.normals[3 * index.normal_index + 2]
            };
        }

        // 纹理坐标
        if (!attrib.texcoords.empty()) {
            vertex.TexCoords = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                attrib.texcoords[2 * index.texcoord_index + 1] // 反转 V 轴
            };
        }
        

        newMesh.Vertices.push_back(vertex);
        newMesh.Indices.push_back(static_cast<UINT>(newMesh.Vertices.size()) - 1);
    }
    ComputeTangents(newMesh.Vertices, newMesh.Indices);
    const size_t vertexDataSize = newMesh.Vertices.size() * sizeof(Vertex);
    const size_t indexDataSize = newMesh.Indices.size() * sizeof(UINT);

    // 创建 GPU 资源 - 顶点缓冲区
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(vertexDataSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&newMesh.vertexBuffer)));

    newMesh.vbv.BufferLocation = newMesh.vertexBuffer->GetGPUVirtualAddress();
    newMesh.vbv.SizeInBytes = static_cast<UINT>(vertexDataSize);
    newMesh.vbv.StrideInBytes = sizeof(Vertex);

    // 创建 GPU 资源 - 索引缓冲区
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES{ D3D12_HEAP_TYPE_UPLOAD },
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(indexDataSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&newMesh.indexBuffer)));

    newMesh.ibv.BufferLocation = newMesh.indexBuffer->GetGPUVirtualAddress();
    newMesh.ibv.SizeInBytes = static_cast<UINT>(indexDataSize);
    newMesh.ibv.Format = DXGI_FORMAT_R32_UINT;

    // 复制顶点数据
    UINT8* data = nullptr;
    ThrowIfFailed(newMesh.vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data)));
    memcpy(data, newMesh.Vertices.data(), vertexDataSize);
    newMesh.vertexBuffer->Unmap(0, nullptr);

    // 复制索引数据
    ThrowIfFailed(newMesh.indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data)));
    memcpy(data, newMesh.Indices.data(), indexDataSize);
    newMesh.indexBuffer->Unmap(0, nullptr);

    return newMesh;
}
void ComputeTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        Vertex& v0 = vertices[indices[i + 0]];
        Vertex& v1 = vertices[indices[i + 1]];
        Vertex& v2 = vertices[indices[i + 2]];

        // 顶点坐标
        XMVECTOR pos0 = XMLoadFloat3(&v0.Position);
        XMVECTOR pos1 = XMLoadFloat3(&v1.Position);
        XMVECTOR pos2 = XMLoadFloat3(&v2.Position);

        // 计算两个边
        XMVECTOR edge1 = XMVectorSubtract(pos1, pos0);
        XMVECTOR edge2 = XMVectorSubtract(pos2, pos0);

        // UV 坐标
        XMFLOAT2 uv0 = v0.TexCoords;
        XMFLOAT2 uv1 = v1.TexCoords;
        XMFLOAT2 uv2 = v2.TexCoords;

        float deltaU1 = uv1.x - uv0.x;
        float deltaV1 = uv1.y - uv0.y;
        float deltaU2 = uv2.x - uv0.x;
        float deltaV2 = uv2.y - uv0.y;

        // 计算 TBN 矩阵的逆矩阵分母
        float determinant = (deltaU1 * deltaV2 - deltaU2 * deltaV1);
        float f = (determinant == 0.0f) ? 1.0f : (1.0f / determinant);

        // 计算 Tangent 和 Bitangent
        XMVECTOR tangent = XMVectorScale(XMVectorSubtract(
            XMVectorScale(edge1, deltaV2),
            XMVectorScale(edge2, deltaV1)), f);

        XMVECTOR bitangent = XMVectorScale(XMVectorSubtract(
            XMVectorScale(edge2, deltaU1),
            XMVectorScale(edge1, deltaU2)), f);

        // 累加到顶点数据
        XMFLOAT3 t, b;
        XMStoreFloat3(&t, tangent);
        XMStoreFloat3(&b, bitangent);

        v0.Tangent = t;
        v1.Tangent = t;
        v2.Tangent = t;

        v0.BiTangent = b;
        v1.BiTangent = b;
        v2.BiTangent = b;
    }

    // 归一化 Tangent
    for (auto& vertex : vertices)
    {
        XMVECTOR t = XMLoadFloat3(&vertex.Tangent);
        XMVECTOR n = XMLoadFloat3(&vertex.Normal);

        // 正交化 Tangent
        t = XMVector3Normalize(XMVectorSubtract(t, XMVectorScale(n, XMVector3Dot(n, t).m128_f32[0])));

        XMStoreFloat3(&vertex.Tangent, t);
    }
}
