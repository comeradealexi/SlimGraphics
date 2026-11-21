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
		// Sorts maximum_elements (a power of two) elements of array_to_sort starting at element_offset. Elements at or beyond
		// the count in array_size[counter_index].x are treated as empty and sort to the end.
		void sort(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer, sg::UnorderedAccessView& array_to_sort, sg::UnorderedAccessView& array_size, sg::u32 maximum_elements, SortDirection sort_direction,
			sg::u32 element_offset = 0, sg::u32 counter_index = 0);

	private:

		struct Pipelines
		{
			sg::SharedPtr<sg::Pipeline> pipeline_local;
			sg::SharedPtr<sg::Pipeline> pipeline_global;
		};

		Pipelines pipelines[SortDirection::Count];
	};
}