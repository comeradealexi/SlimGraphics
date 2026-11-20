#pragma once
#include <sgTypes.h>
#include "sgVulkanInclude.h"

namespace sg
{
	namespace Vulkan
	{
		// Pipeline statistics queries.
		//
		// Unlike D3D12, Vulkan does not allow input assembly / vertex / geometry / clipping statistics to be active while mesh
		// shader draws execute. Each application level query is therefore recorded as a sequence of segments: "classic" segments
		// (all classic counters) around vertex pipeline work and "mesh" segments (fragment, compute, task and mesh counters) around
		// mesh draws. The command list switches segments as needed and the results are summed back into one PipelineStatistics.
		class GPUStatisticPool
		{
		public:
			GPUStatisticPool(const vk::raii::Device& device, bool mesh_shader_queries, u32 max_count);
			using Index = u32;

			void resolve();
			void begin_frame();
			void end_frame(CommandList* command_list);

			Index allocate_new_query();

			void begin_query(Index index, CommandList* command_list);
			void end_query(Index index, CommandList* command_list);

			PipelineStatistics collect_query(Index idx);

			// Used by CommandList. Returns false if no segment could be started (mesh segments without mesh shader queries).
			bool begin_segment(const vk::raii::CommandBuffer& command_buffer, Index logical_index, bool mesh);
			void end_segment(const vk::raii::CommandBuffer& command_buffer);
			bool supports_mesh_segments() const { return mesh_shader_queries; }

		private:
			struct Segment
			{
				Index logical_index = 0;
				bool mesh = false;
				u32 physical_index = 0;
			};

			bool recording = false;
			const u32 max_count;
			const u32 max_segments;
			const bool mesh_shader_queries;
			u32 current_index = 0;
			u32 classic_used = 0;
			u32 mesh_used = 0;
			bool segment_open = false;
			vk::raii::QueryPool classic_pool = nullptr;
			vk::raii::QueryPool mesh_pool = nullptr;
			std::vector<Segment> segments;
			std::vector<PipelineStatistics> results;
		};
	}
}
