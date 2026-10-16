#pragma once
#include <sgPlatformInclude.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <unordered_map>
#include "sgUploadHeap.h"
#include "gdrShaderStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "DebugDraw.h"
#include "gdrJSON.h"
#include "gdrEditor.h"
#include "gdrModel.h"

namespace gdr
{
	using sg::Ptr;
	using sg::UploadHeap;

	template <typename T>
	struct GPUBuffer
	{
		std::vector<T>				cpu_data;
		sg::SharedPtr<sg::Buffer>	buffer;
		sg::UnorderedAccessView		uav;
		sg::ShaderResourceView		srv;

		void create_and_upload_to_gpu(sg::Ptr<sg::UploadHeap>& upload_heap);
	};

	struct ModelBuffers
	{
		GPUBuffer<ShaderStructs::ModelData>		model_data;
		GPUBuffer<ShaderStructs::MaterialData>	material_data;
		GPUBuffer<ShaderStructs::MeshletData>	meshlet_data;
	};

	class Scene
	{
		friend class Editor;
	public:
		Scene(sg::SharedPtr<sg::Device>& _device);
		void update(Ptr<UploadHeap>& upload_heap);
		void render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw);
		void load_gpu_data(Ptr<UploadHeap>& upload_heap);

	private:
		SceneNode root_scene_node;
		Editor editor;
		std::unordered_map<std::string, Ptr<Model>> models;

		sg::SharedPtr<sg::Device> device;

		ModelBuffers model_buffers;
	};
}