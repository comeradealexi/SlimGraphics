#include "Model.h"

//assimp
#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>
#include <limits>
#include <vector>
#include <DirectXMath.h>

using namespace DirectX;
using namespace sg;

Model::Model(Device* device, UploadHeap* upload_heap, const std::string_view file)
{
	Assimp::Importer ai_importer;
	const int flags = aiProcess_Triangulate
		| aiProcess_PreTransformVertices
		| aiProcess_CalcTangentSpace
		| aiProcess_GenSmoothNormals
		| aiProcess_JoinIdenticalVertices
		| aiProcess_SplitLargeMeshes;

	const aiScene* aScene = ai_importer.ReadFile(file.data(), flags); //By passing file path, allows assimp to auto load material files.
	seAssert(aScene != nullptr, "Failed To Load Assimp Scene %s\n", file.data());
	if (aScene)
	{
		//Mesh Data
		{
			std::vector<Vertex> Vertices;
			std::vector<uint32_t> Indices;

			mesh_parts.resize(aScene->mNumMeshes);
			for (uint32_t mesh_idx = 0; mesh_idx < mesh_parts.size(); mesh_idx++)
			{
				MeshPart& mesh = mesh_parts[mesh_idx];
				aiMesh* aMesh = aScene->mMeshes[mesh_idx];

				mesh.material_index = aMesh->mMaterialIndex;
				mesh.vb_offset = (uint32_t)Vertices.size();
				mesh.ib_offset = (uint32_t)Indices.size();

				for (uint32_t vert_idx = 0; vert_idx < aMesh->mNumVertices; vert_idx++)
				{
					Vertex v;

					v.Position = XMFLOAT3(&aMesh->mVertices[vert_idx].x);
					v.UV = aMesh->mTextureCoords[0] ? XMFLOAT2(&aMesh->mTextureCoords[0][vert_idx].x) : XMFLOAT2(0, 0);
					v.Normal = XMFLOAT3(&aMesh->mNormals[vert_idx].x);
					v.Tangent = aMesh->mTangents ? XMFLOAT3(&aMesh->mTangents[vert_idx].x) : XMFLOAT3(0, 0, 0);
					Vertices.push_back(v);
					//mesh.m_verticesCPU.push_back(v);
				}

				mesh.draw_count = aMesh->mNumFaces * 3;

				uint32_t uStartIndex = Indices.size();
				for (uint32_t face_idx = 0; face_idx < aMesh->mNumFaces; face_idx++)
				{
					Indices.push_back(aMesh->mFaces[face_idx].mIndices[0]);
					Indices.push_back(aMesh->mFaces[face_idx].mIndices[1]);
					Indices.push_back(aMesh->mFaces[face_idx].mIndices[2]);
				}
			}

			//Vertex Buffer
			{
				const size_t vertex_byte_size = Vertices.size() * sizeof(Vertex);
				SharedPtr<Memory> vb_mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, vertex_byte_size, 1024 * 64);
				vb = device->create_buffer(vb_mem, vertex_byte_size, 1024 * 64, BufferType::Vertex, false);
				
				UploadHeap::Offset upload_offset = upload_heap->allocate_upload_memory(vertex_byte_size, 256);
				upload_heap->write_upload_memory(upload_offset, Vertices.data(), vertex_byte_size);
				upload_heap->upload_to_buffer(vb.get(), 0, upload_offset, vertex_byte_size);
			}

			//Index Buffer
			{
				const size_t index_byte_size = Indices.size() * sizeof(uint32_t);
				SharedPtr<Memory> ib_mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, index_byte_size, 1024 * 64);
				ib = device->create_buffer(ib_mem, index_byte_size, 1024 * 64, BufferType::Index, false);

				UploadHeap::Offset upload_offset = upload_heap->allocate_upload_memory(index_byte_size, 256);
				upload_heap->write_upload_memory(upload_offset, Indices.data(), index_byte_size);
				upload_heap->upload_to_buffer(ib.get(), 0, upload_offset, index_byte_size);
			}			
		}
		ai_importer.FreeScene();
	}
}
