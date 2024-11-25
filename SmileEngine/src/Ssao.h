 //***************************************************************************************
 // Ssao.h by Frank Luna (C) 2015 All Rights Reserved.
 //***************************************************************************************
 
 #ifndef SSAO_H
 #define SSAO_H
 
 #pragma once
 
#include "../../Core/src/d3dUtil.h"
#include "../../Core/src/ConstantBuffers.h"
#include <vector>
 
 
enum SrvHeapLayout : uint32_t
{  
    viewNormal,
    viewDepth,
    randomVector,
    sceneColor0,
    sceneColor1,
};


class Ssao
{
public:

    Ssao(ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        UINT width, UINT height);
    Ssao(const Ssao& rhs) = delete;
    Ssao& operator=(const Ssao& rhs) = delete;
    ~Ssao() = default;

    void Initialize();

    static const DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
    static const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    static const int MaxBlurRadius = 5;

    UINT SsaoMapWidth()const;
    UINT SsaoMapHeight()const;

    void GetOffsetVectors(DirectX::XMFLOAT4 offsets[14]);
    std::vector<float> CalcGaussWeights(float sigma);

    ID3D12Resource* NormalMap();
    CD3DX12_CPU_DESCRIPTOR_HANDLE NormalMapRtv();
    ID3D12Resource* AmbientMap();

    void BuildDescriptors(
        ID3D12Resource* depthStencilBuffer);

    void RebuildDescriptors(ID3D12Resource* depthStencilBuffer);

    ///<summary>
    /// Call when the backbuffer is resized.  
    ///</summary>
    void OnResize(UINT newWidth, UINT newHeight);

    ///<summary>
    /// Changes the render target to the Ambient render target and draws a fullscreen
    /// quad to kick off the pixel shader to compute the AmbientMap.  We still keep the
    /// main depth buffer binded to the pipeline, but depth buffer read/writes
    /// are disabled, as we do not need the depth buffer computing the Ambient map.
    ///</summary>
    void ComputeSsao(
        ID3D12GraphicsCommandList* cmdList,
        SsaoConstants& ssaoConstants,
        int blurCount);

    ID3D12DescriptorHeap* SsaoSrvHeap() { return m_SrvHeap.Get(); }
    ID3D12DescriptorHeap* SsaoRtvHeap() { return m_RtvHeap.Get(); }

 private:
 
 	///<summary>
 	/// Blurs the ambient map to smooth out the noise caused by only taking a
 	/// few random samples per pixel.  We use an edge preserving blur so that 
 	/// we do not blur across discontinuities--we want edges to remain edges.
 	///</summary>
 	void BlurAmbientMap(ID3D12GraphicsCommandList* cmdList, int blurCount);
 	void BlurAmbientMap(ID3D12GraphicsCommandList* cmdList, bool horzBlur);
 
 	void CreateResources();
 	void BuildRandomVectorTexture(ID3D12GraphicsCommandList* cmdList);
 
 	void BuildOffsetVectors();
 
 
 private:
 	ID3D12Device* md3dDevice;
 
 	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
 
 	ID3D12PipelineState* m_SsaoPso = nullptr;
 	ID3D12PipelineState* m_BlurPso = nullptr;
 
 	Microsoft::WRL::ComPtr<ID3D12Resource> mRandomVectorMap;
 	Microsoft::WRL::ComPtr<ID3D12Resource> mRandomVectorMapUploadBuffer;

 	Microsoft::WRL::ComPtr<ID3D12Resource> m_ViewNormal;
 	Microsoft::WRL::ComPtr<ID3D12Resource> m_SceneColor0;
 	Microsoft::WRL::ComPtr<ID3D12Resource> m_SceneColor1;
 
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;

    CD3DX12_CPU_DESCRIPTOR_HANDLE m_NormalRtv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_SceneColor0Rtv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_SceneColor1Rtv;

 	UINT mRenderTargetWidth;
 	UINT mRenderTargetHeight;
 
 	DirectX::XMFLOAT4 mOffsets[14];
 
 	D3D12_VIEWPORT mViewport;
 	D3D12_RECT mScissorRect;
 };
 
 #endif // SSAO_H