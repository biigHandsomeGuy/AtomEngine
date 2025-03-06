#pragma once


using Microsoft::WRL::ComPtr;

struct Texture
{
	std::string Name;
	std::string Filename;

	ComPtr<ID3D12Resource> Resource;

	ComPtr<ID3D12Resource> UploadHeap;
};