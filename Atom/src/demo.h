//#include "Atom.h"
//#include "MathHelper.h"
//#include "UploadBuffer.h" 
//#include "Camera.h"
//#include "d3dUtil.h"
//
//using Microsoft::WRL::ComPtr;
//using namespace DirectX;
//
//// Vertex definition.
//struct Vertex
//{
//	XMFLOAT4 position;
//};
//
//__declspec(align(256)) struct SceneConstantBuffer
//{
//	XMFLOAT4 velocity = XMFLOAT4(0, 0, 0, 0);
//	XMFLOAT4 offset = XMFLOAT4(0, 0, 0, 0);
//	XMFLOAT4 color = XMFLOAT4(1, 0, 0, 0);
//	XMFLOAT4X4 projection = MathHelper::Identity4x4();
//};
//
//enum class HeapLayout : UINT8
//{
//	kHeapsCount
//};
//
//enum GraphicsRootParameters
//{
//	kCbv,
//	kGraphicsRootParametersCount
//};
//enum ComputeRootParameters
//{
//	kMeshConstants,
//
//};
//
//struct IndirectCommand
//{
//	D3D12_GPU_VIRTUAL_ADDRESS cbv;
//	D3D12_VERTEX_BUFFER_VIEW vbv;
//	D3D12_DRAW_ARGUMENTS drawArguments;
//};
//
//class Demo final : public Application
//{
//public:
//
//	Demo(HINSTANCE hInstance);
//	Demo(const Demo& rhs) = delete;
//	Demo& operator=(const Demo& rhs) = delete;
//	~Demo();
//
//	virtual bool Initialize()override;
//
//private:
//	virtual void CreateRtvAndDsvDescriptorHeaps()override;
//	virtual void OnResize()override;
//	virtual void Update(const GameTimer& gt)override;
//	virtual void Draw(const GameTimer& gt)override;
//
//
//private:
//
//	ComPtr<ID3D12RootSignature> m_RootSignature = nullptr;
//	ComPtr<ID3D12DescriptorHeap> m_SrvDescriptorHeap = nullptr;
//
//
//	std::unordered_map<std::string, ComPtr<ID3DBlob>> m_Shaders;
//	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_PSOs;
//
//
//	Camera m_Camera;
//
//	ComPtr<ID3D12Resource> m_TriangleVertexBuffer;
//	ComPtr<ID3D12Resource> m_RectangleVertexBuffer;
//	ComPtr<ID3D12Resource> m_ConstantBuffer;
//	ComPtr<ID3D12Resource> m_CommandBuffer;
//
//	ComPtr<ID3D12CommandSignature> m_CommandSignature;
//
//	D3D12_VERTEX_BUFFER_VIEW m_TriangleVertexBufferView;
//	D3D12_VERTEX_BUFFER_VIEW m_RectangleVertexBufferView;
//
//	std::vector<SceneConstantBuffer> m_ConstantBufferData;
//
//	UINT8* m_pCbvDataBegin = nullptr;
//};
//
