#include "pch.h"
#include "TextureManager.h"
#include "GraphicsCommon.h"

using namespace Graphics;

class ManagedTexture : public Texture
{
	friend class TextureRef;
public:
	ManagedTexture(const std::wstring& fileName);

	void WaitForLoad() const;
	void CreateFromMemory(char* memory, eDefaultTexture fallbak, bool forceSRGB);
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
	std::map<std::wstring, std::unique_ptr<ManagedTexture>> s_TextureCache;

	std::mutex s_Mutex;

	void Destroy(const std::wstring& key)
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

void ManagedTexture::Unload()
{

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
