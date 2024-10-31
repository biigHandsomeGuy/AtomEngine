#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

struct Texture
{
	std::string Name;
	std::string Filename;

	ComPtr<ID3D12Resource> Resource;

	ComPtr<ID3D12Resource> UploadHeap;

};