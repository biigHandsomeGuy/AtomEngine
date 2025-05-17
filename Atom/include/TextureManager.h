#pragma once

#include "Texture.h"
#include "GraphicsCommon.h"

class TextureRef;

namespace TextureManager
{
	using namespace Graphics;

	void Initialize(const std::wstring& rootPath);
	void Shutdown();

	TextureRef LoadTexFromFile(const std::wstring& filePath, eDefaultTexture = kMagenta2D, bool sRGB = false);
	TextureRef LoadHdrFromFile(const std::wstring& filePath);
}

class ManagedTexture;

class TextureRef
{
public:
	TextureRef(const TextureRef& ref);
	TextureRef(ManagedTexture* tex = nullptr);
	~TextureRef();

    void operator= (std::nullptr_t);
    void operator= (TextureRef& rhs);

    // Check that this points to a valid texture (which loaded successfully)
    bool IsValid() const;

    // Gets the SRV descriptor handle.  If the reference is invalid,
    // returns a valid descriptor handle (specified by the fallback)
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRV() const;

    // Get the texture pointer.  Client is responsible to not dereference
    // null pointers.
    const Texture* Get(void) const;

    const Texture* operator->(void) const;
private:
	ManagedTexture* m_Ref = nullptr;
};

