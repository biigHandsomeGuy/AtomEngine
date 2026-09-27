#pragma once

#include <vector>
#include <unordered_map>
#include <mutex>
#include <string>
#include <memory>

#include "Texture.h"
class CommandContext;
class TextureManager;

// The last owner releases both the texture and its descriptor pair. Retain this
// owner through every upload/render fence before dropping it on the render thread.
// TextureManager must outlive these owners.
class TextureResidency
{
public:
    ~TextureResidency();
    TextureID Id() const { return m_Id; }
    uint64_t Bytes() const { return m_Bytes; }
    TextureResidency(const TextureResidency&) = delete;
    TextureResidency& operator=(const TextureResidency&) = delete;
private:
    friend class TextureManager;
    TextureResidency(TextureManager& manager, TextureID id, uint64_t bytes)
        : m_Manager(manager), m_Id(id), m_Bytes(bytes) {}
    TextureManager& m_Manager;
    TextureID m_Id;
    uint64_t m_Bytes;
};

struct TextureResidencyStats
{
    size_t LiveTextures = 0;
    size_t OwnedTextures = 0;
    uint64_t ResidentBytes = 0;
    uint64_t OwnedBytes = 0;
    uint64_t DescriptorReuses = 0;
    size_t FreeDescriptorPairs = 0;
};

enum class TextureUsage
{
	TextureFromData,
	TextureFromPath,
	HDRTextureFromPath,
	RenderTarget,
	DepthStencil,
	UAVTexture,
	CubeMap
};

//
struct TextureCreationDesc
{
	TextureUsage usage{};
	uint32_t width{};
	uint32_t height{};
	DXGI_FORMAT format{ DXGI_FORMAT_R8G8B8A8_UNORM };
	uint32_t mipLevels{ 1 };
	uint32_t depthOrArraySize{ 1 };
	uint32_t bytesPerPixel{ 4 };
	std::wstring path{};
};

class TextureManager
{
public:

	void Initialize();
	void Shutdown();

	TextureID LoadTexture(const std::wstring& path, bool srgb = true);
	TextureID LoadTextureFromMemory(
		const void* data,
		size_t size,
		bool srgb = true);
	TextureID LoadHDR(const std::wstring& path);

	TextureID CreateTexture2D(
		uint32_t width,
		uint32_t height,
		DXGI_FORMAT format,
		const void* data = nullptr, CommandContext* uploadContext = nullptr);

    std::shared_ptr<const TextureResidency> CreateOwnedTexture2D(
        uint32_t width, uint32_t height, DXGI_FORMAT format,
        const void* data, CommandContext& uploadContext);
    TextureResidencyStats Stats() const;

	Texture& Get(TextureID id);
	const Texture& Get(TextureID id) const;

	uint32_t GetBindlessIndex(TextureID id) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSrvHandle(TextureID id) const;
    uint32_t GetNullBindlessIndex() const { return m_NullSrvIndex; }

private:

    friend class TextureResidency;
    void ReleaseOwned(TextureID id);
	TextureID CreateTextureInternal(
		const TextureCreationDesc& desc,
		const void* data, CommandContext* uploadContext = nullptr);

private:

    struct Entry
    {
        std::unique_ptr<Texture> Resource;
        uint64_t Bytes = 0;
        bool Owned = false;
    };
    struct DescriptorPair
    {
        uint32_t BindlessIndex;
        D3D12_CPU_DESCRIPTOR_HANDLE Cpu;
    };
    // IDs never repeat, including across Initialize/Shutdown. Erased IDs fail
    // lookup rather than aliasing a newly allocated texture/descriptor slot.
    std::unordered_map<TextureID, Entry> m_Textures;
    std::vector<DescriptorPair> m_FreeDescriptors;
    TextureID m_NextTextureId = 1;
    uint64_t m_DescriptorReuses = 0;
	std::unordered_map<std::wstring, TextureID> m_PathMap;

	mutable std::recursive_mutex m_Mutex;
    uint32_t m_NullSrvIndex = INVALID_TEXTURE;
};
