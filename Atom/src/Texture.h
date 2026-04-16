#pragma once

#include "GpuResource.h"

using TextureID = uint32_t;

static constexpr TextureID INVALID_TEXTURE = 0xffffffff;

// Responsible for GPU Resource, SRV descriptor, bindless resource index
class Texture : public GpuResource
{
public:
	Texture()
	{
		m_SRVCpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		m_UAVCpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		m_RTVCpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		m_DSVCpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
	}

	void Destroy() override
	{
		GpuResource::Destroy();
		m_SRVCpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		m_UAVCpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		m_RTVCpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
		m_DSVCpuDescriptorHandle.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
	}

	//const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const { return m_hCpuDescriptorHandle; }

	uint32_t GetWidth() const { return m_Width; }
	uint32_t GetHeight() const { return m_Height; }
	uint32_t GetDepth() const { return m_Depth; }
public:
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const { return m_SRVCpuDescriptorHandle; }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetUAV() const { return m_UAVCpuDescriptorHandle; }
	uint32_t GetBindlessSRVIndex() const { return m_SRVIndex; }
	uint32_t GetBindlessUAVIndex() const { return m_UAVIndex; }
	TextureID GetID() const { return m_ID; }
protected:
	friend class TextureManager;
	
	TextureID m_ID = INVALID_TEXTURE;

	uint32_t m_Width = 0;
	uint32_t m_Height = 0;
	uint32_t m_Depth = 1;
	// Bindless Resource
	uint32_t m_SRVIndex = INVALID_TEXTURE;
	uint32_t m_UAVIndex = INVALID_TEXTURE;

	D3D12_CPU_DESCRIPTOR_HANDLE m_SRVCpuDescriptorHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_UAVCpuDescriptorHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_RTVCpuDescriptorHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_DSVCpuDescriptorHandle;
};

