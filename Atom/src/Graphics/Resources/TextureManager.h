#pragma once

#include <vector>
#include <unordered_map>
#include <mutex>
#include <string>

#include "Texture.h"
class CommandContext;

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

	Texture& Get(TextureID id);
	const Texture& Get(TextureID id) const;

	uint32_t GetBindlessIndex(TextureID id) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetSrvHandle(TextureID id) const;
    uint32_t GetNullBindlessIndex() const { return m_NullSrvIndex; }

private:


	TextureID CreateTextureInternal(
		const TextureCreationDesc& desc,
		const void* data, CommandContext* uploadContext = nullptr);

private:

	std::vector<Texture> m_Textures;
	std::unordered_map<std::wstring, TextureID> m_PathMap;

	std::recursive_mutex m_Mutex;
    uint32_t m_NullSrvIndex = INVALID_TEXTURE;
};
