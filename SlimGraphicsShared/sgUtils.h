#pragma once
#include "seEngine.h"
#include "sgTypes.h"
#include <sgPlatformInclude.h>

namespace sg
{
	template <typename T>
	static inline constexpr T AlignUp(T val, T align)
	{
		return (val + align - 1) / align * align;
	}

	SharedPtr<VertexShader> create_vertex_shader(Device& device, const char* file_path);
	SharedPtr<PixelShader> create_pixel_shader(Device& device, const char* file_path);
	SharedPtr<ComputeShader> create_compute_shader(Device& device, const char* file_path);
	SharedPtr<MeshShader> create_mesh_shader(Device& device, const char* file_path);
	SharedPtr<AmplificationShader> create_amplification_shader(Device& device, const char* file_path);

	SharedPtr<Texture> create_texture(Device& device, const ResourceCreateDesc& resource_create_desc);
	SharedPtr<Buffer> create_buffer(Device& device, u32 size, BufferType type = BufferType::GeneralDataBuffer, bool uav_access = true);
	SharedPtr<Buffer> create_readback_buffer(Device& device, u32 size);
}