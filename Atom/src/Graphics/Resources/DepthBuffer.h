#pragma once

#include "PixelBuffer.h"

class DepthBuffer : public PixelBuffer
{
public:
    DepthBuffer(float ClearDepth = 1.0f, uint8_t ClearStencil = 0)
        : m_ClearDepth(ClearDepth), m_ClearStencil(ClearStencil)
    {
        m_hDSV[0].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        m_hDSV[1].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        m_hDSV[2].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        m_hDSV[3].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        m_hDepthSRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        m_hStencilSRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
    }

    // Create a depth buffer.
    void Create(const std::wstring& Name, uint32_t Width, uint32_t Height, DXGI_FORMAT Format);

    // Get pre-created CPU-visible descriptor handles
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV() const { return m_hDSV[0]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_DepthReadOnly() const { return m_hDSV[1]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_StencilReadOnly() const { return m_hDSV[2]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_ReadOnly() const { return m_hDSV[3]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetDepthSRV() const { return m_hDepthSRV; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetStencilSRV() const { return m_hStencilSRV; }

    float GetClearDepth() const { return m_ClearDepth; }
    uint8_t GetClearStencil() const { return m_ClearStencil; }

protected:
	void CreateDerivedViews( ID3D12Device* Device, DXGI_FORMAT Format );

    float m_ClearDepth;
    uint8_t m_ClearStencil;
    // default      read_only_depth     read_only_stencil       read_only
    D3D12_CPU_DESCRIPTOR_HANDLE m_hDSV[4];
    D3D12_CPU_DESCRIPTOR_HANDLE m_hDepthSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hStencilSRV;

};
