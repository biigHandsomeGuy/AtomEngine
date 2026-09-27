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

    m_Textures.emplace_back();
}

void TextureManager::Shutdown()
{
    lock_guard lock(m_Mutex);

    for (auto& tex : m_Textures)
        tex.Destroy();

    m_Textures.clear();
    m_PathMap.clear();
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
    Texture tex{};

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

    auto handle = Graphics::g_BindlessHeap.AllocBindless();
    const auto cpuDescriptor = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    Graphics::g_Device->CreateShaderResourceView(tex.m_pResource.Get(), &srvDesc, cpuDescriptor);
    Graphics::g_Device->CopyDescriptorsSimple(1, handle.Handle, cpuDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // CPU mirrors can be copied into dynamic tables; shader-visible descriptors
    // are write-only from the CPU and cannot be used as CopyDescriptors sources.
    tex.m_SRVCpuDescriptorHandle = cpuDescriptor;
    tex.m_SRVIndex = handle.Index;

    TextureID id = static_cast<TextureID>(m_Textures.size());
    tex.m_ID = id;
    m_Textures.push_back(std::move(tex));

    return id;
}

Texture& TextureManager::Get(TextureID id)
{
    return m_Textures[id];
}

const Texture& TextureManager::Get(TextureID id) const
{
    return m_Textures[id];
}

uint32_t TextureManager::GetBindlessIndex(TextureID id) const
{
    return id == INVALID_TEXTURE ? INVALID_TEXTURE : m_Textures[id].GetBindlessSRVIndex();
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandle(TextureID id) const
{
    return id == INVALID_TEXTURE ? D3D12_CPU_DESCRIPTOR_HANDLE{} : m_Textures[id].GetSRV();
}
