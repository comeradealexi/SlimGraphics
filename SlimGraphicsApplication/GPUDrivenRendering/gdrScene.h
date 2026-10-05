#pragma once
#include <sgPlatformInclude.h>
#include <unordered_map>
#include "sgUploadHeap.h"
#include "Model.h"
#include "ShaderSharedStructures.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "DebugDraw.h"
#include "gdrJSON.h"
#include "gdrEditor.h"
#include "gdrModel.h"
#include "sgUploadHeap.h"

namespace gdr
{
	using sg::Ptr;
	using sg::UploadHeap;
	class Scene
	{
		friend class Editor;
	public:
		Scene(sg::SharedPtr<sg::Device>& _device);
		void update(Ptr<UploadHeap>& upload_heap);
		void render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw);
		Ptr<Model> load_model(Ptr<UploadHeap>& upload_heap, const char* file_path);
		void load_models(Ptr<UploadHeap>& upload_heap);

	private:
		SceneNode root_scene_node;
		Editor editor;
		std::unordered_map<std::string, Ptr<Model>> models;

		sg::SharedPtr<sg::Device> device;
	};
}