#pragma once

#include "Texture.h"

class ManagedTexture;

class TextureRef
{
public:
	TextureRef(const TextureRef& ref);
	TextureRef(ManagedTexture* tex = nullptr);
	~TextureRef();


private:
	ManagedTexture* m_Ref = nullptr;
};

