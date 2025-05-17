#include "pch.h"
#include "CommandContext.h"
#include "GraphicsCore.h"
#include "TextureManager.h"
#include "stb_image/stb_image.h"
#include "d3dUtil.h"
using namespace Graphics;
using namespace DirectX;
using namespace DirectX::PackedVector;
std::vector<XMHALF4> ConvertToHalf(const float* floatData, int pixelCount) {
	std::vector<XMHALF4> halfData(pixelCount);
	for (int i = 0; i < pixelCount; i++) {
		halfData[i] = XMHALF4(
			floatData[i * 4 + 0],  // R
			floatData[i * 4 + 1],  // G
			floatData[i * 4 + 2],  // B
			floatData[i * 4 + 3]   // A
		);
	}
	return halfData;
}
class ManagedTexture : public Texture
{
	friend class TextureRef;
public:
	ManagedTexture(const std::wstring& fileName);

	void WaitForLoad() const;
	void CreateFromMemory(unsigned char* data, uint64_t width, uint64_t height, eDefaultTexture fallbak, bool forceSRGB);
	void CreateFromMemory(float* data, uint64_t width, uint64_t height);
private:
	void Unload();
	bool IsValid() const { return m_IsValid; }
private:
	std::wstring m_MapKey; // for deleting from map later
	bool m_IsValid = false;
	bool m_IsLoading = true;
	size_t m_ReferenceCount = 0;

};

namespace TextureManager
{
	std::wstring s_RootPath = L"";
	std::map<std::wstring, std::unique_ptr<ManagedTexture>> s_TextureCache;

	std::mutex s_Mutex;
	ManagedTexture* FindOrLoadTexture(const std::wstring& fileName, eDefaultTexture fallback, bool forceSRGB);
	void Initialize(const std::wstring& rootPath)
	{
		s_RootPath = rootPath;
	}

	void Shutdown(void)
	{
		s_TextureCache.clear();
	}

	TextureRef LoadTexFromFile(const std::wstring& filePath, eDefaultTexture fallback, bool sRGB)
	{
		return FindOrLoadTexture(filePath, fallback, sRGB);
	}

	TextureRef LoadHdrFromFile(const std::wstring& filePath)
	{
		ManagedTexture* tex = nullptr;

		{
			std::lock_guard<std::mutex> Guard(s_Mutex);

			std::wstring key = filePath;

			// If a texture was already created make sure it has finished loading before
			// returning a point to it.
			if (auto iter = s_TextureCache.find(key); iter != s_TextureCache.end())
			{
				tex = iter->second.get();
				tex->WaitForLoad();
				return tex;
			}
			else
			{
				// If it's not found, create a new managed texture and start loading it

				tex = new ManagedTexture(key);
				s_TextureCache[key].reset(tex);
			}
		}

		// Create Texture
		int width = 0, height = 0, channels = 0;
		stbi_set_flip_vertically_on_load(false);
		auto data = stbi_loadf("D:/AtomEngine/Atom/Assets/Textures/EnvirMap/marry.hdr", &width, &height, &channels, 4);
		if (!data) {
			printf("stbi_loadf failed: %s\n", stbi_failure_reason());
		}
		tex->CreateFromMemory(data, width, height);
		// This was the first time it was requested, so indicate that the caller must read the file
		return tex;
	}



	ManagedTexture* FindOrLoadTexture(const std::wstring& fileName, eDefaultTexture fallback, bool forceSRGB)
	{
		ManagedTexture* tex = nullptr;

		{
			std::lock_guard<std::mutex> Guard(s_Mutex);

			std::wstring key = fileName;
			//if (forceSRGB)
				//key += L"_sRGB";

			// If a texture was already created make sure it has finished loading before
			// returning a point to it.
			if (auto iter = s_TextureCache.find(key); iter != s_TextureCache.end())
			{
				tex = iter->second.get();
				tex->WaitForLoad();
				return tex;
			}
			else
			{
				// If it's not found, create a new managed texture and start loading it

				tex = new ManagedTexture(key);
				s_TextureCache[key].reset(tex);
			}
		}

		int width = 0, height = 0, channels = 0;

		auto data = stbi_load(Utility::WideStringToUTF8(fileName).c_str(), &width, &height, &channels, STBI_rgb_alpha);

		tex->CreateFromMemory(data, width, height, fallback, forceSRGB);
		// This was the first time it was requested, so indicate that the caller must read the file
		return tex;
	}


	void DestroyTexture(const std::wstring& key)
	{
		std::lock_guard<std::mutex> Guard(s_Mutex);

		if (auto iter = s_TextureCache.find(key); iter != s_TextureCache.end())
		{
			s_TextureCache.erase(iter);
		}
	}
}


ManagedTexture::ManagedTexture(const std::wstring& fileName)
	:m_MapKey(fileName), m_IsValid(false), m_IsLoading(true), m_ReferenceCount(0)
{
}

void ManagedTexture::WaitForLoad() const
{
	while ((volatile bool&)m_IsLoading)
		std::this_thread::yield();
}

void ManagedTexture::CreateFromMemory(unsigned char* data, uint64_t width, uint64_t height, eDefaultTexture fallback, bool forceSRGB)
{
	if (data == nullptr)
	{
		m_hCpuDescriptorHandle = GetDefaultTexture(fallback);
	}
	else
	{
		// We probably have a texture to load, so let's allocate a new descriptor
		m_hCpuDescriptorHandle = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.MipLevels = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		//if (forceSRGB)
		//	textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

		ThrowIfFailed(g_Device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(m_pResource.GetAddressOf())
		));
		D3D12_SUBRESOURCE_DATA textureData = {};
		textureData.pData = data;
		textureData.RowPitch = width * 4;
		textureData.SlicePitch = textureData.RowPitch * height;

		GpuResource destTexture(m_pResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
		CommandContext::InitializeTexture(destTexture, 1, &textureData);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = textureDesc.Format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = -1;
		srvDesc.Texture2D.MostDetailedMip = 0;

		g_Device->CreateShaderResourceView(m_pResource.Get(), &srvDesc, m_hCpuDescriptorHandle);

		m_IsValid = true;
	}

	m_IsLoading = false;
}

void ManagedTexture::CreateFromMemory(float* data, uint64_t width, uint64_t height)
{
	std::vector<XMHALF4> halfData = ConvertToHalf(data, width * height);
	// We probably have a texture to load, so let's allocate a new descriptor
	m_hCpuDescriptorHandle = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;


	ThrowIfFailed(g_Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(m_pResource.GetAddressOf())
	));
	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = halfData.data();
	textureData.RowPitch = width * 4 * 2;
	textureData.SlicePitch = textureData.RowPitch * height;

	GpuResource destTexture(m_pResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
	CommandContext::InitializeTexture(destTexture, 1, &textureData);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = textureDesc.Format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = -1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	g_Device->CreateShaderResourceView(m_pResource.Get(), &srvDesc, m_hCpuDescriptorHandle);

	m_IsValid = true;
	

	m_IsLoading = false;
}

void ManagedTexture::Unload()
{
	TextureManager::DestroyTexture(m_MapKey);
}


TextureRef::TextureRef(const TextureRef& ref)
	:m_Ref(ref.m_Ref)
{
	if (m_Ref != nullptr)
		++m_Ref->m_ReferenceCount;
}

TextureRef::TextureRef(ManagedTexture* tex)
	:m_Ref(tex)
{
	if (m_Ref != nullptr)
		++m_Ref->m_ReferenceCount;
}

TextureRef::~TextureRef()
{
	if (m_Ref != nullptr && --m_Ref->m_ReferenceCount == 0)
		m_Ref->Unload();
}
void TextureRef::operator= (std::nullptr_t)
{
	if (m_Ref != nullptr)
		--m_Ref->m_ReferenceCount;

	m_Ref = nullptr;
}

void TextureRef::operator= (TextureRef& rhs)
{
	if (m_Ref != nullptr)
		--m_Ref->m_ReferenceCount;

	m_Ref = rhs.m_Ref;

	if (m_Ref != nullptr)
		++m_Ref->m_ReferenceCount;
}

bool TextureRef::IsValid() const
{
	return m_Ref && m_Ref->IsValid();
}

const Texture* TextureRef::Get(void) const
{
	return m_Ref;
}

const Texture* TextureRef::operator->(void) const
{
	assert(m_Ref != nullptr);
	return m_Ref;
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureRef::GetSRV() const
{
	if (m_Ref != nullptr)
		return m_Ref->GetSRV();
	else
		return GetDefaultTexture(kMagenta2D);
}
