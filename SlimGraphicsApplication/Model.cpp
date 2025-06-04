#include "Model.h"

//assimp
#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>

#include <limits>
#include <vector>
#include <DirectXMath.h>

#include <sgPlatformInclude.h>

#include <meshoptimizer.h>

#include <GeometricPrimitive.h>

using namespace DirectX;
using namespace sg;

static DirectX::XMFLOAT3 SetAbsMax(const DirectX::XMFLOAT3& a, DirectX::XMFLOAT3& b)
{
	DirectX::XMFLOAT3 o;

	o.x = (fabsf(a.x) > fabsf(b.x)) ? fabsf(a.x) : fabsf(b.x);
	o.y = (fabsf(a.y) > fabsf(b.y)) ? fabsf(a.y) : fabsf(b.y);
	o.z = (fabsf(a.z) > fabsf(b.z)) ? fabsf(a.z) : fabsf(b.z);

	return o;
}

static DirectX::XMFLOAT3 Max(const DirectX::XMFLOAT3& a, DirectX::XMFLOAT3& b)
{
	DirectX::XMFLOAT3 o;
	o.x = std::max<float>(a.x, b.x);
	o.y = std::max<float>(a.y, b.y);
	o.z = std::max<float>(a.z, b.z);
	return o;
}

static DirectX::XMFLOAT3 Min(const DirectX::XMFLOAT3& a, DirectX::XMFLOAT3& b)
{
	DirectX::XMFLOAT3 o;
	o.x = std::min<float>(a.x, b.x);
	o.y = std::min<float>(a.y, b.y);
	o.z = std::min<float>(a.z, b.z);
	return o;
}

Model::Model(Device* device, UploadHeap* upload_heap, const InitData& _init_data) : init_data(_init_data)
{
	Assimp::Importer ai_importer;
	const int flags = aiProcess_Triangulate
		| aiProcess_PreTransformVertices
		| aiProcess_CalcTangentSpace
		| aiProcess_GenSmoothNormals
		| aiProcess_JoinIdenticalVertices
		| aiProcess_SplitLargeMeshes;

	const aiScene* aScene = ai_importer.ReadFile(init_data.file_path.data(), flags); //By passing file path, allows assimp to auto load material files.
	seAssert(aScene != nullptr, "Failed To Load Assimp Scene %s\n", init_data.file_path.c_str());
	if (aScene)
	{
		u32 stride;
		sg::InputLayout::Desc input_layout = Vertex::make_input_layout(&stride);

		//Mesh Data
		{
			mesh_parts.clear();
			mesh_parts.resize(aScene->mNumMeshes);
			for (uint32_t mesh_idx = 0; mesh_idx < mesh_parts.size(); mesh_idx++)
			{
				MeshPart& mesh = mesh_parts[mesh_idx];
				aiMesh* aMesh = aScene->mMeshes[mesh_idx];

				mesh.material_index = aMesh->mMaterialIndex;

				const bool use_color = aMesh->mColors[0] != nullptr;

				for (uint32_t vert_idx = 0; vert_idx < aMesh->mNumVertices; vert_idx++)
				{
					Vertex v;

					v.Position = XMFLOAT3(&aMesh->mVertices[vert_idx].x);
					v.Color = use_color ? XMFLOAT3(aMesh->mColors[0][vert_idx].r, aMesh->mColors[0][vert_idx].g, aMesh->mColors[0][vert_idx].b): XMFLOAT3(1.0f,1.0f,1.0f);
					v.UV = aMesh->mTextureCoords[0] ? XMFLOAT2(&aMesh->mTextureCoords[0][vert_idx].x) : XMFLOAT2(0, 0);
					v.Normal = XMFLOAT3(&aMesh->mNormals[vert_idx].x);
					v.Tangent = aMesh->mTangents ? XMFLOAT3(&aMesh->mTangents[vert_idx].x) : XMFLOAT3(0, 0, 0);
					mesh.vertices.push_back(v);
					//mesh.m_verticesCPU.push_back(v);
					max_extent = SetAbsMax(max_extent, v.Position);
					mesh.max_extent = SetAbsMax(mesh.max_extent, v.Position);

					bounding_box_max = Max(bounding_box_max, v.Position);
					mesh.bounding_box_max = Max(mesh.bounding_box_max, v.Position);

					bounding_box_min = Min(bounding_box_min, v.Position);
					mesh.bounding_box_min = Min(mesh.bounding_box_min, v.Position);
				}

				// AABB
				{
					const float dist_x = fabsf(bounding_box_max.x - bounding_box_min.x);
					const float dist_y = fabsf(bounding_box_max.y - bounding_box_min.y);
					const float dist_z = fabsf(bounding_box_max.z - bounding_box_min.z);
					mesh.aabb.Center.x = bounding_box_min.x + (dist_x / 2);
					mesh.aabb.Center.y = bounding_box_min.y + (dist_y / 2);
					mesh.aabb.Center.z = bounding_box_min.z + (dist_z / 2);
					mesh.aabb.Extents.x = dist_x / 2;
					mesh.aabb.Extents.y = dist_y / 2;
					mesh.aabb.Extents.z = dist_z / 2;

				}

				mesh.vertex_count = aMesh->mNumVertices;

				for (uint32_t face_idx = 0; face_idx < aMesh->mNumFaces; face_idx++)
				{
					// For some reason we're getting a face idx through where mNumIndices is 2! But the documentation lies! 
					// "Together with the #aiProcess_Triangulate flag you can then be sure that *#aiFace::mNumIndices is always 3."
					
					//seAssert(aMesh->mFaces[face_idx].mNumIndices == 3, "Expected 3 indicies per face...");					
					if (aMesh->mFaces[face_idx].mNumIndices == 3)
					{
						mesh.indices.push_back(aMesh->mFaces[face_idx].mIndices[0]);
						mesh.indices.push_back(aMesh->mFaces[face_idx].mIndices[1]);
						mesh.indices.push_back(aMesh->mFaces[face_idx].mIndices[2]);
					}
				}

				if (init_data.meshopt_simplification)
				{
					float threshold = init_data.meshopt_simplification_threshold;
					size_t target_index_count = size_t(mesh.indices.size() * threshold);
					float target_error = init_data.meshopt_simplification_target_error;

					std::vector<uint32_t> new_indices;
					new_indices.resize(mesh.indices.size());
					float lod_error = 0.f;
					new_indices.resize(meshopt_simplify(new_indices.data(), mesh.indices.data(), mesh.indices.size(), (const float*)mesh.vertices.data(), mesh.vertices.size(), sizeof(Vertex), target_index_count, target_error, /* options= */ 0, &lod_error));
					mesh.indices = new_indices;
				}

				if (init_data.meshopt_vertex_cache)
				{
					std::vector<uint32_t> new_indices;
					new_indices.resize(mesh.indices.size());
					meshopt_optimizeVertexCache(new_indices.data(), mesh.indices.data(), mesh.indices.size(), mesh.vertices.size());
					mesh.indices = new_indices;
				}

				//if (init_data.meshopt_overdraw)
				//{
				//	std::vector<uint32_t> new_indices;
				//	new_indices.resize(mesh.indices.size());
				//	meshopt_optimizeVertexCache(new_indices.data(), mesh.indices.data(), mesh.indices.size(), mesh.vertices.size());
				//	mesh.indices = new_indices;
				//}

				mesh.draw_count = mesh.indices.size();

				// Make meshlets
				if (init_data.meshopt_meshlets && device->SupportsMeshShaders())
				{
					const size_t max_vertices = 64;
					const size_t max_triangles = 124;
					const float cone_weight = 0.0f;
					size_t max_meshlets = meshopt_buildMeshletsBound(mesh.indices.size(), max_vertices, max_triangles);
					MeshShadingData& mesh_shader_data = mesh.mesh_shader_data;
					mesh_shader_data.meshlets.resize(max_meshlets);
					mesh_shader_data.meshlet_vertices.resize(max_meshlets * max_vertices);
					mesh_shader_data.meshlet_triangles.resize(max_meshlets * max_triangles * 3);

					size_t meshlet_count = meshopt_buildMeshlets(mesh_shader_data.meshlets.data(), mesh_shader_data.meshlet_vertices.data(), mesh_shader_data.meshlet_triangles.data(), mesh.indices.data(),
						mesh.indices.size(), (float*) &mesh.vertices[0], mesh.vertices.size(), sizeof(Vertex), max_vertices, max_triangles, cone_weight);

					// Trim excess of vectors
					const meshopt_Meshlet& last = mesh_shader_data.meshlets[meshlet_count - 1];
					mesh_shader_data.meshlet_vertices.resize(last.vertex_offset + last.vertex_count);
					mesh_shader_data.meshlet_triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
					mesh_shader_data.meshlets.resize(meshlet_count);

					if (init_data.meshopt_meshlet_optimize)
					{
						for (meshopt_Meshlet& m : mesh_shader_data.meshlets)
						{
							meshopt_optimizeMeshlet(&mesh_shader_data.meshlet_vertices[m.vertex_offset], &mesh_shader_data.meshlet_triangles[m.triangle_offset], m.triangle_count, m.vertex_count);
						}
					}

					// Compute meshlet bounds
					mesh_shader_data.meshlet_bounds.resize(mesh_shader_data.meshlets.size());
					size_t meshlet_idx = 0;
					for (meshopt_Meshlet& m : mesh_shader_data.meshlets)
					{
						mesh_shader_data.meshlet_bounds[meshlet_idx] = meshopt_computeMeshletBounds(&mesh_shader_data.meshlet_vertices[m.vertex_offset], &mesh_shader_data.meshlet_triangles[m.triangle_offset],
							m.triangle_count, (float*) &mesh.vertices[0], mesh.vertices.size(), sizeof(Vertex));
						meshlet_idx++;
					}

					//seAssert(mesh_shader_data.meshlet_triangles.size() % 3 == 0, "Size must be divisible by 3");

					for (meshopt_Meshlet& meshlet : mesh_shader_data.meshlets)
					{
						const u32 new_triangle_offset = mesh_shader_data.meshlet_triangles_gpu.size();
						for (size_t tri_idx = 0; tri_idx < meshlet.triangle_count; tri_idx++)
						{
							const u32 tri_offset = meshlet.triangle_offset + (tri_idx * 3);
							seAssert((tri_offset + 2) < mesh_shader_data.meshlet_triangles.size(), "Reading too many tris");
							u32 tri = 0;
							tri |= static_cast<u32>(mesh_shader_data.meshlet_triangles[tri_offset + 0]) << 0u;
							tri |= static_cast<u32>(mesh_shader_data.meshlet_triangles[tri_offset + 1]) << 8u;
							tri |= static_cast<u32>(mesh_shader_data.meshlet_triangles[tri_offset + 2]) << 16u;

							mesh_shader_data.meshlet_triangles_gpu.push_back(tri);
						}
						meshlet.triangle_offset = new_triangle_offset;
					}

					//for (size_t i = 0; i < mesh_shader_data.meshlet_triangles.size(); i+=3)
					//{
					//	u32 tri = 0;
					//	tri |= static_cast<u32>(mesh_shader_data.meshlet_triangles[i + 0]) << 0u;
					//	tri |= static_cast<u32>(mesh_shader_data.meshlet_triangles[i + 1]) << 8u;
					//	tri |= static_cast<u32>(mesh_shader_data.meshlet_triangles[i + 2]) << 16u;
					//
					//	mesh_shader_data.meshlet_triangles_gpu.push_back(tri);
					//}
					//
					//for (meshopt_Meshlet& meshlet : mesh_shader_data.meshlets)
					//{
					//	meshlet.triangle_offset /= 3;
					//}


					// Create GPU Buffers
					{ // Meshlets
						const size_t meshlet_byte_size = mesh_shader_data.meshlets.size() * sizeof(mesh_shader_data.meshlets[0]);
						SharedPtr<Memory> ml_mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, meshlet_byte_size, 1024 * 64);
						mesh_shader_data.gpu_meshlets = device->create_buffer(ml_mem, meshlet_byte_size, 1024 * 64, BufferType::GeneralDataBuffer, true);
						mesh_shader_data.gpu_meshlets_view = device->create_unordered_access_view(mesh_shader_data.gpu_meshlets, sizeof(mesh_shader_data.meshlets[0]), mesh_shader_data.meshlets.size());
						mesh_shader_data.gpu_meshlets_view_srv = device->create_shader_resource_view(mesh_shader_data.gpu_meshlets, sizeof(mesh_shader_data.meshlets[0]), mesh_shader_data.meshlets.size());

						UploadHeap::Offset upload_offset = upload_heap->allocate_upload_memory(meshlet_byte_size, 256);
						upload_heap->write_upload_memory(upload_offset, mesh_shader_data.meshlets.data(), meshlet_byte_size);
						upload_heap->upload_to_buffer(mesh_shader_data.gpu_meshlets.get(), 0, upload_offset, meshlet_byte_size);
					}
					{
						const size_t unique_meshlet_verts_byte_size = mesh_shader_data.meshlet_vertices.size() * sizeof(mesh_shader_data.meshlet_vertices[0]);
						SharedPtr<Memory> ml_mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, unique_meshlet_verts_byte_size, 1024 * 64);
						mesh_shader_data.gpu_unique_vertex_indices = device->create_buffer(ml_mem, unique_meshlet_verts_byte_size, 1024 * 64, BufferType::GeneralDataBuffer, true);
						mesh_shader_data.gpu_unique_vertex_indices_view = device->create_unordered_access_view(mesh_shader_data.gpu_unique_vertex_indices, sizeof(mesh_shader_data.meshlet_vertices[0]), mesh_shader_data.meshlet_vertices.size());
						mesh_shader_data.gpu_unique_vertex_indices_view_srv = device->create_shader_resource_view(mesh_shader_data.gpu_unique_vertex_indices, sizeof(mesh_shader_data.meshlet_vertices[0]), mesh_shader_data.meshlet_vertices.size());

						UploadHeap::Offset upload_offset = upload_heap->allocate_upload_memory(unique_meshlet_verts_byte_size, 256);
						upload_heap->write_upload_memory(upload_offset, mesh_shader_data.meshlet_vertices.data(), unique_meshlet_verts_byte_size);
						upload_heap->upload_to_buffer(mesh_shader_data.gpu_unique_vertex_indices.get(), 0, upload_offset, unique_meshlet_verts_byte_size);
					}
					{
						const size_t primitive_indices_byte_size = mesh_shader_data.meshlet_triangles_gpu.size() * sizeof(mesh_shader_data.meshlet_triangles_gpu[0]);
						SharedPtr<Memory> ml_mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, primitive_indices_byte_size, 1024 * 64);
						mesh_shader_data.gpu_primitive_indices = device->create_buffer(ml_mem, primitive_indices_byte_size, 1024 * 64, BufferType::GeneralDataBuffer, true);
						mesh_shader_data.gpu_primitive_indices_view = device->create_unordered_access_view(mesh_shader_data.gpu_primitive_indices, sizeof(mesh_shader_data.meshlet_triangles_gpu[0]), mesh_shader_data.meshlet_triangles_gpu.size());
						mesh_shader_data.gpu_primitive_indices_view_srv = device->create_shader_resource_view(mesh_shader_data.gpu_primitive_indices, sizeof(mesh_shader_data.meshlet_triangles_gpu[0]), mesh_shader_data.meshlet_triangles_gpu.size());

						UploadHeap::Offset upload_offset = upload_heap->allocate_upload_memory(primitive_indices_byte_size, 256);
						upload_heap->write_upload_memory(upload_offset, mesh_shader_data.meshlet_triangles_gpu.data(), primitive_indices_byte_size);
						upload_heap->upload_to_buffer(mesh_shader_data.gpu_primitive_indices.get(), 0, upload_offset, primitive_indices_byte_size);
					}
				}
			}

			if (init_data.scalemodel1to1)
			{
				float position_multiplier = 1.0f / std::max(std::max(max_extent.x, max_extent.y), max_extent.z);
				max_extent = {};
				bounding_box_min = { FLT_MAX, FLT_MAX, FLT_MAX };
				bounding_box_max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

				for (uint32_t mesh_idx = 0; mesh_idx < mesh_parts.size(); mesh_idx++)
				{
					MeshPart& mesh = mesh_parts[mesh_idx];
					mesh.max_extent = {};
					mesh.bounding_box_min = { FLT_MAX, FLT_MAX, FLT_MAX };
					mesh.bounding_box_max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

					for (Vertex& v : mesh.vertices)
					{
						v.Position.x *= position_multiplier;
						v.Position.y *= position_multiplier;
						v.Position.z *= position_multiplier;

						max_extent = SetAbsMax(max_extent, v.Position);
						mesh.max_extent = SetAbsMax(mesh.max_extent, v.Position);

						bounding_box_max = Max(bounding_box_max, v.Position);
						mesh.bounding_box_max = Max(mesh.bounding_box_max, v.Position);

						bounding_box_min = Min(bounding_box_min, v.Position);
						mesh.bounding_box_min = Min(mesh.bounding_box_min, v.Position);
					}
				}
			}

			std::vector<Vertex> vertices;
			std::vector<uint32_t> indices;
			for (uint32_t mesh_idx = 0; mesh_idx < mesh_parts.size(); mesh_idx++)
			{
				MeshPart& mesh = mesh_parts[mesh_idx];
				mesh.vb_offset = (uint32_t)vertices.size();
				mesh.ib_offset = (uint32_t)indices.size();
				vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
				indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
			}

			//Vertex Buffer
			{
				const size_t vertex_byte_size = vertices.size() * sizeof(Vertex);
				SharedPtr<Memory> vb_mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, vertex_byte_size, 1024 * 64);
				vb = device->create_buffer(vb_mem, vertex_byte_size, 1024 * 64, BufferType::Vertex, false);
				vbv = device->create_vertex_buffer_view(vb, 0, vertex_byte_size, stride);
				vb_srv = device->create_shader_resource_view(vb, sizeof(Vertex), vertices.size());
				
				UploadHeap::Offset upload_offset = upload_heap->allocate_upload_memory(vertex_byte_size, 256);
				upload_heap->write_upload_memory(upload_offset, vertices.data(), vertex_byte_size);
				upload_heap->upload_to_buffer(vb.get(), 0, upload_offset, vertex_byte_size);
			}

			//Index Buffer
			{
				const size_t index_byte_size = indices.size() * sizeof(uint32_t);
				SharedPtr<Memory> ib_mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, index_byte_size, 1024 * 64);
				ib = device->create_buffer(ib_mem, index_byte_size, 1024 * 64, BufferType::Index, false);
				ibv = device->create_index_buffer_view(ib, 0, index_byte_size, DXGI_FORMAT_R32_UINT);

				UploadHeap::Offset upload_offset = upload_heap->allocate_upload_memory(index_byte_size, 256);
				upload_heap->write_upload_memory(upload_offset, indices.data(), index_byte_size);
				upload_heap->upload_to_buffer(ib.get(), 0, upload_offset, index_byte_size);
			}			
		}
		ai_importer.FreeScene();
	}
}

void Model::SetPipeline(sg::Ptr<sg::Pipeline> new_pipeline)
{
	pipeline = std::move(new_pipeline);
}

void Model::Render(sg::CommandList* command_list, sg::ConstantBufferView& cbv_camera, sg::ConstantBufferView& cbv_model)
{
	seAssert(pipeline != nullptr, "Can't render model without pipeline.");

	command_list->set_pipeline(pipeline.get());

	Binding b;
	b.cbv_binding_count = 2;
	b.set_cbv(cbv_camera, 0);
	b.set_cbv(cbv_model, 1);
	command_list->bind(b, PipelineType::Geometry);

	command_list->bind_vertex_buffer(vbv);
	command_list->bind_index_buffer(ibv);
	for (MeshPart& mesh_part : mesh_parts)
	{		
		command_list->draw_indexed_instanced(mesh_part.draw_count, 1, mesh_part.ib_offset, mesh_part.vb_offset, 0);
	}
}
