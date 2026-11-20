#include "pch.h"
#include "sgVulkanGPUStatisticPool.h"
#include "sgVulkanCommandList.h"

namespace sg
{
	namespace Vulkan
	{
		namespace
		{
			using PS = vk::QueryPipelineStatisticFlagBits;
			// Results are written in order of increasing flag bit
			const vk::QueryPipelineStatisticFlags kClassicStatistics =
				PS::eInputAssemblyVertices | PS::eInputAssemblyPrimitives | PS::eVertexShaderInvocations |
				PS::eGeometryShaderInvocations | PS::eGeometryShaderPrimitives | PS::eClippingInvocations | PS::eClippingPrimitives |
				PS::eFragmentShaderInvocations | PS::eTessellationControlShaderPatches | PS::eTessellationEvaluationShaderInvocations |
				PS::eComputeShaderInvocations;
			constexpr u32 kClassicCounterCount = 11;
			// The counters allowed while mesh shader draws execute
			const vk::QueryPipelineStatisticFlags kMeshStatistics = PS::eFragmentShaderInvocations | PS::eComputeShaderInvocations | PS::eTaskShaderInvocationsEXT | PS::eMeshShaderInvocationsEXT;
			constexpr u32 kMeshCounterCount = 4;

			// Segments per application query (switches between vertex and mesh work within one query)
			constexpr u32 kSegmentsPerQuery = 16;

			bool get_results(const vk::raii::QueryPool& pool, u32 count, u32 counters, std::vector<u64>& out)
			{
				out.assign(static_cast<size_t>(count) * counters, 0);
				if (count == 0)
				{
					return true;
				}
				const size_t stride = sizeof(u64) * counters;
				const VkResult result = pool.getDispatcher()->vkGetQueryPoolResults(static_cast<VkDevice>(pool.getDevice()), static_cast<VkQueryPool>(*pool), 0, count,
					count * stride, out.data(), stride, VK_QUERY_RESULT_64_BIT);
				seAssert(result == VK_SUCCESS || result == VK_NOT_READY, "vkGetQueryPoolResults failed (%i)", static_cast<int>(result));
				return result == VK_SUCCESS;
			}
		}

		GPUStatisticPool::GPUStatisticPool(const vk::raii::Device& device, bool mesh_shader_queries_, u32 _max_count)
			: max_count(_max_count), max_segments(_max_count * kSegmentsPerQuery), mesh_shader_queries(mesh_shader_queries_)
		{
			results.resize(max_count);
			segments.reserve(max_segments);
			classic_pool = vk::raii::QueryPool(device, vk::QueryPoolCreateInfo({}, vk::QueryType::ePipelineStatistics, max_segments, kClassicStatistics));
			classic_pool.reset(0, max_segments);
			if (mesh_shader_queries)
			{
				mesh_pool = vk::raii::QueryPool(device, vk::QueryPoolCreateInfo({}, vk::QueryType::ePipelineStatistics, max_segments, kMeshStatistics));
				mesh_pool.reset(0, max_segments);
			}
		}

		void GPUStatisticPool::resolve()
		{
			seAssert(recording == false, "Recording should be false");
			std::vector<u64> classic;
			std::vector<u64> mesh;
			get_results(classic_pool, classic_used, kClassicCounterCount, classic);
			if (mesh_shader_queries)
			{
				get_results(mesh_pool, mesh_used, kMeshCounterCount, mesh);
			}

			for (u32 i = 0; i < current_index; i++)
			{
				results[i] = PipelineStatistics();
			}
			for (const Segment& segment : segments)
			{
				PipelineStatistics& s = results[segment.logical_index];
				if (segment.mesh)
				{
					const u64* r = mesh.data() + static_cast<size_t>(segment.physical_index) * kMeshCounterCount;
					s.ps_invocations += r[0];
					s.cs_invocations += r[1];
					s.as_invocations += r[2];
					s.ms_invocations += r[3];
				}
				else
				{
					const u64* r = classic.data() + static_cast<size_t>(segment.physical_index) * kClassicCounterCount;
					s.ia_vertices += r[0];
					s.ia_primitives += r[1];
					s.vs_invocations += r[2];
					s.gs_invocations += r[3];
					s.gs_primitives += r[4];
					s.clipping_invocations += r[5];
					s.clipping_primitives += r[6];
					s.ps_invocations += r[7];
					s.hs_invocations += r[8];
					s.ds_invocations += r[9];
					s.cs_invocations += r[10];
				}
			}
		}

		void GPUStatisticPool::begin_frame()
		{
			seAssert(recording == false, "Recording should be false");
			// Reset last frame's queries from the CPU (the previous frame has completed)
			if (classic_used > 0) classic_pool.reset(0, classic_used);
			if (mesh_used > 0) mesh_pool.reset(0, mesh_used);
			classic_used = 0;
			mesh_used = 0;
			segments.clear();
			recording = true;
			current_index = 0;
		}

		void GPUStatisticPool::end_frame(CommandList*)
		{
			seAssert(recording, "Recording should be true");
			seAssert(!segment_open, "A statistics query is still active");
			recording = false;
		}

		GPUStatisticPool::Index GPUStatisticPool::allocate_new_query()
		{
			seAssert(current_index < max_count, "GPUStatisticPool exhausted (max %u)", max_count);
			if (current_index >= max_count)
			{
				return max_count - 1;
			}
			return current_index++;
		}

		void GPUStatisticPool::begin_query(Index index, CommandList* command_list)
		{
			command_list->begin_statistics_query(this, index);
		}

		void GPUStatisticPool::end_query(Index, CommandList* command_list)
		{
			command_list->end_statistics_query();
		}

		bool GPUStatisticPool::begin_segment(const vk::raii::CommandBuffer& command_buffer, Index logical_index, bool mesh)
		{
			seAssert(!segment_open, "Statistics segment already open");
			if (mesh && !mesh_shader_queries)
			{
				return false;
			}
			u32& used = mesh ? mesh_used : classic_used;
			seAssert(used < max_segments, "Too many statistics query segments this frame");
			if (used >= max_segments)
			{
				return false;
			}
			const u32 physical_index = used++;
			segments.push_back({ logical_index, mesh, physical_index });
			command_buffer.beginQuery(mesh ? *mesh_pool : *classic_pool, physical_index, {});
			segment_open = true;
			return true;
		}

		void GPUStatisticPool::end_segment(const vk::raii::CommandBuffer& command_buffer)
		{
			seAssert(segment_open, "No statistics segment open");
			const Segment& segment = segments.back();
			command_buffer.endQuery(segment.mesh ? *mesh_pool : *classic_pool, segment.physical_index);
			segment_open = false;
		}

		PipelineStatistics GPUStatisticPool::collect_query(Index idx)
		{
			return results[idx];
		}
	}
}
