#pragma once
#include <array>
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

	class GPUSort
	{
	public:
		enum SortDirection
		{
			Ascending,
			Descending,
			Count
		};

		GPUSort(sg::SharedPtr<sg::Device>& device);
		void sort(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer, sg::UnorderedAccessView& array_to_sort, sg::UnorderedAccessView& array_size, sg::u32 maximum_elements, SortDirection sort_direction);

	private:

		struct Pipelines
		{
			sg::SharedPtr<sg::Pipeline> pipeline_local;
			sg::SharedPtr<sg::Pipeline> pipeline_global;
		};

		Pipelines pipelines[SortDirection::Count];
	};
}