#pragma once

#include <d3d12.h>
#include <DirectXMath.h>
#include <vector>
#include <memory>
#include <string>




class Mesh
{
public:
	struct Vertex
	{
		DirectX::XMFLOAT3 Position;
		DirectX::XMFLOAT3 Normal;
		DirectX::XMFLOAT2 TexCoords;
		DirectX::XMFLOAT3 Tangent;
		DirectX::XMFLOAT3 BiTangent;
	};

	struct Face
	{
		uint32_t v1, v2, v3;
	};

	static std::unique_ptr<Mesh> fromFile(const std::string fileName);

	const std::vector<Vertex>& vertices() const { return m_Vertices; }
	const std::vector<Face>& faces() const { return m_Faces; }

private:
	Mesh(const struct aiMesh* mesh);

	std::vector<Vertex> m_Vertices;
	std::vector<Face> m_Faces;

};