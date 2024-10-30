#include "Mesh.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include <stdexcept>
namespace
{
    const unsigned int ImportFlags =
        aiProcess_CalcTangentSpace | aiProcess_Triangulate;
}

Mesh::Mesh(const aiMesh* mesh)
{
	assert(mesh->HasNormals());
	assert(mesh->HasPositions());

	m_Vertices.reserve(mesh->mNumVertices);
	for (size_t i = 0; i < m_Vertices.capacity(); i++)
	{
		Vertex vertex;
		vertex.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
		vertex.Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
		if (mesh->HasTangentsAndBitangents()) {
			vertex.Tangent = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
			vertex.BiTangent = { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z };
		}
		if (mesh->HasTextureCoords(0)) {
			vertex.TexCoords = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
		}
		m_Vertices.push_back(vertex);
	}

	m_Faces.reserve(mesh->mNumFaces);
	for (size_t i = 0; i < m_Faces.capacity(); i++)
	{
		m_Faces.push_back({ mesh->mFaces[i].mIndices[0],mesh->mFaces[i].mIndices[1] ,mesh->mFaces[i].mIndices[2] });


	}
}

std::unique_ptr<Mesh> Mesh::fromFile(const std::string fileName)
{
    Assimp::Importer importer;

	std::unique_ptr<Mesh> mesh;

    const aiScene* scene = importer.ReadFile(fileName, ImportFlags);
	if (scene && scene->HasMeshes()) {
		mesh = std::unique_ptr<Mesh>(new Mesh{ scene->mMeshes[0] });
	}
	else {
		throw std::runtime_error("Failed to load mesh file: " + fileName);
	}
	return mesh;
}
