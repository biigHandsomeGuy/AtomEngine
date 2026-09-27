#include "pch.h"
#include "TextureManager.h"

#include <stb_image.h>
#include <format>
#include <stdexcept>

#include "GraphicsCore.h"
#include "CommandContext.h"
#include "Renderer.h"

using namespace std;

static DXGI_FORMAT ToSRGB(DXGI_FORMAT fmt)
{
    if (fmt == DXGI_FORMAT_R8G8B8A8_UNORM)
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    return fmt;
}

void TextureManager::Initialize()
{
    lock_guard lock(m_Mutex);
    m_Textures.clear();
    m_PathMap.clear();

    m_FreeDescriptors.clear();
    m_DescriptorReuses = 0;
    const auto descriptor = Graphics::g_BindlessHeap.AllocBindless();
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
    nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrv.Texture2D.MipLevels = 1;
    Graphics::g_Device->CreateShaderResourceView(nullptr, &nullSrv, descriptor.Handle);
    m_NullSrvIndex = descriptor.Index;
}

void TextureManager::Shutdown()
{
    lock_guard lock(m_Mutex);

    m_Textures.clear();
    m_PathMap.clear();
    m_FreeDescriptors.clear();
    m_NullSrvIndex = INVALID_TEXTURE;
}

TextureID TextureManager::LoadTexture(const std::wstring& path, bool srgb)
{
    scoped_lock lock(m_Mutex);

    if (m_PathMap.contains(path))
        return m_PathMap[path];

    int w, h, comp;
    unsigned char* data = stbi_load(
        std::string(path.begin(), path.end()).c_str(),
        &w, &h, &comp, 4);

    if (!data)
        throw std::runtime_error("Failed to load texture");

    TextureCreationDesc desc{};
    desc.usage = TextureUsage::TextureFromData;
    desc.width = w;
    desc.height = h;
    desc.format = srgb ? ToSRGB(DXGI_FORMAT_R8G8B8A8_UNORM) : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.bytesPerPixel = 4;

    TextureID id = CreateTextureInternal(desc, data);

    stbi_image_free(data);

    m_PathMap[path] = id;
    return id;
}

TextureID TextureManager::LoadTextureFromMemory(const void* data, size_t size, bool srgb)
{
    scoped_lock lock(m_Mutex);

    int w, h, comp;
    unsigned char* decoded = stbi_load_from_memory(
        static_cast<const stbi_uc*>(data),
        static_cast<int>(size),
        &w, &h, &comp, 4);

    if (!decoded)
        throw std::runtime_error("Failed to load texture from memory");

    TextureCreationDesc desc{};
    desc.usage = TextureUsage::TextureFromData;
    desc.width = w;
    desc.height = h;
    desc.format = srgb ? ToSRGB(DXGI_FORMAT_R8G8B8A8_UNORM) : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.bytesPerPixel = 4;

    TextureID id = CreateTextureInternal(desc, decoded);
    stbi_image_free(decoded);
    return id;
}

TextureID TextureManager::LoadHDR(const std::wstring& path)
{
    scoped_lock lock(m_Mutex);

    if (m_PathMap.contains(path))
        return m_PathMap[path];

    int w, h, comp;
    float* data = stbi_loadf(
        std::string(path.begin(), path.end()).c_str(),
        &w, &h, &comp, 4);

    if (!data)
        throw std::runtime_error("Failed to load hdr texture");

    TextureCreationDesc desc{};
    desc.usage = TextureUsage::HDRTextureFromPath;
    desc.width = w;
    desc.height = h;
    desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.bytesPerPixel = sizeof(float) * 4;

    TextureID id = CreateTextureInternal(desc, data);

    stbi_image_free(data);

    m_PathMap[path] = id;
    return id;
}

TextureID TextureManager::CreateTexture2D(
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format,
    const void* data, CommandContext* uploadContext)
{
    TextureCreationDesc desc{};
    desc.usage = TextureUsage::TextureFromData;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.bytesPerPixel = static_cast<uint32_t>(format == DXGI_FORMAT_R32G32B32A32_FLOAT ? sizeof(float) * 4 : 4);

    return CreateTextureInternal(desc, data, uploadContext);
}

TextureID TextureManager::CreateTextureInternal(const TextureCreationDesc& desc, const void* data, CommandContext* uploadContext)
{
    scoped_lock lock(m_Mutex);
    if (m_NextTextureId == INVALID_TEXTURE)
        throw std::runtime_error("Texture ID space exhausted.");
    auto texture = std::make_unique<Texture>();
    auto& tex = *texture;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = desc.format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ASSERT_SUCCEEDED(Graphics::g_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(tex.m_pResource.GetAddressOf())));

    tex.m_Width = desc.width;
    tex.m_Height = desc.height;
    tex.m_UsageState = D3D12_RESOURCE_STATE_COPY_DEST;

    if (data)
    {
        D3D12_SUBRESOURCE_DATA subresource{};
        subresource.pData = data;
        subresource.RowPitch = static_cast<LONG_PTR>(desc.width) * desc.bytesPerPixel;
        subresource.SlicePitch = static_cast<LONG_PTR>(desc.width) * desc.height * desc.bytesPerPixel;

        if (uploadContext)
            uploadContext->UploadTexture(tex, 1, &subresource);
        else
            CommandContext::InitializeTexture(tex, 1, &subresource);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    DescriptorPair pair;
    if (m_FreeDescriptors.empty())
    {
        pair.BindlessIndex = Graphics::g_BindlessHeap.AllocBindless().Index;
        pair.Cpu = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    else
    {
        pair = m_FreeDescriptors.back();
        m_FreeDescriptors.pop_back();
        ++m_DescriptorReuses;
    }
    Graphics::g_Device->CreateShaderResourceView(tex.m_pResource.Get(), &srvDesc, pair.Cpu);
    Graphics::g_Device->CopyDescriptorsSimple(1, Graphics::g_BindlessHeap.GetCpuHandle(pair.BindlessIndex),
        pair.Cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // The CPU mirror and shader-visible slot are recycled together, only after
    // all fenced owners of an owned texture have released it.
    tex.m_SRVCpuDescriptorHandle = pair.Cpu;
    tex.m_SRVIndex = pair.BindlessIndex;
    const TextureID id = m_NextTextureId++;
    tex.m_ID = id;
    const auto bytes = Graphics::g_Device->GetResourceAllocationInfo(0, 1, &resourceDesc).SizeInBytes;
    m_Textures.emplace(id, Entry{ std::move(texture), bytes, false });

    return id;
}

Texture& TextureManager::Get(TextureID id)
{
    scoped_lock lock(m_Mutex);
    return *m_Textures.at(id).Resource;
}

const Texture& TextureManager::Get(TextureID id) const
{
    scoped_lock lock(m_Mutex);
    return *m_Textures.at(id).Resource;
}

uint32_t TextureManager::GetBindlessIndex(TextureID id) const
{
    return id == INVALID_TEXTURE ? INVALID_TEXTURE : Get(id).GetBindlessSRVIndex();
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandle(TextureID id) const
{
    return id == INVALID_TEXTURE ? D3D12_CPU_DESCRIPTOR_HANDLE{} : Get(id).GetSRV();
}

TextureResidency::~TextureResidency()
{
    m_Manager.ReleaseOwned(m_Id);
}

std::shared_ptr<const TextureResidency> TextureManager::CreateOwnedTexture2D(
    uint32_t width, uint32_t height, DXGI_FORMAT format,
    const void* data, CommandContext& context)
{
    scoped_lock lock(m_Mutex);
    const auto id = CreateTexture2D(width, height, format, data, &context);
    auto& entry = m_Textures.at(id);
    entry.Owned = true;
    return std::shared_ptr<const TextureResidency>(new TextureResidency(*this, id, entry.Bytes));
}

void TextureManager::ReleaseOwned(TextureID id)
{
    scoped_lock lock(m_Mutex);
    auto found = m_Textures.find(id);
    if (found == m_Textures.end()) return; // Device shutdown already released it.
    auto& entry = found->second;
    if (!entry.Owned) return;
    m_FreeDescriptors.push_back({ entry.Resource->GetBindlessSRVIndex(), entry.Resource->GetSRV() });
    m_Textures.erase(found);
}

TextureResidencyStats TextureManager::Stats() const
{
    scoped_lock lock(m_Mutex);
    TextureResidencyStats stats;
    stats.LiveTextures = m_Textures.size();
    stats.DescriptorReuses = m_DescriptorReuses;
    stats.FreeDescriptorPairs = m_FreeDescriptors.size();
    for (const auto& [id, entry] : m_Textures)
    {
        stats.ResidentBytes += entry.Bytes;
        if (entry.Owned)
        {
            ++stats.OwnedTextures;
            stats.OwnedBytes += entry.Bytes;
        }
    }
    return stats;
}
