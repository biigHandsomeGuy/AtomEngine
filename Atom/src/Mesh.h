#pragma once
struct Vertex
{
	DirectX::XMFLOAT3 Position;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexCoords;
	DirectX::XMFLOAT3 Tangent;
	DirectX::XMFLOAT3 BiTangent;
};

class Mesh
{
public:
	std::vector<Vertex> Vertices;
	std::vector<UINT> Indices;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;

	D3D12_VERTEX_BUFFER_VIEW vbv;
	D3D12_INDEX_BUFFER_VIEW ibv;
};

