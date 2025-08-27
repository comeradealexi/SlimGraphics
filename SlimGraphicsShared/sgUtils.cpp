#include "sgUtils.h"
#include "seEngineBasicFileIO.h"

namespace sg
{
	SharedPtr<VertexShader> create_vertex_shader(Device& device, const char* file_path)
    {
        std::vector<uint8_t> shader_data = se::BasicFileIO::LoadFile(file_path);
		seAssert(shader_data.size(), "Failed To Load Shader");
        return device.create_vertex_shader(shader_data);
    }

	SharedPtr<PixelShader> create_pixel_shader(Device& device, const char* file_path)
    {
		std::vector<uint8_t> shader_data = se::BasicFileIO::LoadFile(file_path);
		seAssert(shader_data.size(), "Failed To Load Shader");
		return device.create_pixel_shader(shader_data);
    }

	SharedPtr<ComputeShader> create_compute_shader(Device& device, const char* file_path)
    {
		std::vector<uint8_t> shader_data = se::BasicFileIO::LoadFile(file_path);
		seAssert(shader_data.size(), "Failed To Load Shader");
		return device.create_compute_shader(shader_data);
    }

	SharedPtr<MeshShader> create_mesh_shader(Device& device, const char* file_path)
    {
		std::vector<uint8_t> shader_data = se::BasicFileIO::LoadFile(file_path);
		seAssert(shader_data.size(), "Failed To Load Shader");
		return device.create_mesh_shader(shader_data);
    }

	SharedPtr<AmplificationShader> create_amplification_shader(Device& device, const char* file_path)
    {
		std::vector<uint8_t> shader_data = se::BasicFileIO::LoadFile(file_path);
        seAssert(shader_data.size(), "Failed To Load Shader");
		return device.create_amplification_shader(shader_data);
    }

	SharedPtr<Texture> create_texture(Device& device, const ResourceCreateDesc& resource_create_desc)
	{
		SharedPtr<Memory> mem;
		SharedPtr<Texture> tex;
		SizeAndAlignment sal = device.calculate_resource_size_alignment(resource_create_desc);
		MemorySubType memory_type = (bool)(resource_create_desc.usage_flags & (ResourceUsageFlags::RenderTarget | ResourceUsageFlags::DepthStencil)) ? MemorySubType::Target : MemorySubType::Texture;

		mem = device.allocate_memory(MemoryType::GPUOptimal, memory_type, sal.size, sal.alignment);
		tex = device.create_texture(mem, sal.size, resource_create_desc);

		return tex;
	}

	SharedPtr<Buffer> create_buffer(Device& device, u32 size, BufferType type /*= BufferType::GeneralDataBuffer*/, bool uav_access /*= true*/)
	{
		SharedPtr<Memory> mem = device.allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, size);
		SharedPtr<Buffer> buffer = device.create_buffer(mem, size, type, uav_access);
		return buffer;
	}

}