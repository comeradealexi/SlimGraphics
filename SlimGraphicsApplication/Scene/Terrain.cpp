#include "Terrain.h"
#include <lodepng.h>
#include <imgui.h>

// https://learnopengl.com/Guest-Articles/2021/Tessellation/Height-map

using namespace sg;

Terrain::Terrain(SharedPtr<Device>& _device) : device(_device)
{
	height_textures_list.push_back("..\\SlimGraphicsAssets\\Textures\\TestImages\\MultiCol512.png");
	height_textures_list.push_back("..\\SlimGraphicsAssets\\Textures\\iceland_heightmap.png");
	height_textures_list.push_back("..\\SlimGraphicsAssets\\Textures\\snowdon.png");
	height_textures_list.push_back("..\\SlimGraphicsAssets\\Textures\\louisd.png");

	std::vector<uint8_t> vertex_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\Terrain_VertexShader.PC_DXC");
	std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\Terrain_PixelShader.PC_DXC");
}

void Terrain::Update(float delta_time, float total_time, const Camera& camera, DebugDraw& debug_draw)
{
	ImGui::Begin("Terrain Renderer", nullptr, 0);


	ImGui::End();

}

void Terrain::Render(sg::CommandList& command_list, const Camera& camera, sg::ConstantBufferView& cbv_camera, sg::Ptr<UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer, DebugDraw& debug_draw)
{
	if (terrain_texture == nullptr)
	{
		LoadHeightmapTexture(upload_heap);
	}
}

void Terrain::LoadHeightmapTexture(sg::Ptr<UploadHeap>& upload_heap)
{
	std::vector<uint8_t> compressed_png_data = se::BasicFileIO::LoadFile(height_textures_list[current_texture_index].c_str());
	seAssert(compressed_png_data.size(), "Failed to load png data");
	std::vector<uint8_t> bitmap_data;
	unsigned int w, h;
	lodepng::State state;
	if (lodepng::decode(bitmap_data, w, h, compressed_png_data) == 0)
	{
		sg::ResourceCreateDesc rcd;
		rcd.width = w;
		rcd.height = h;
		rcd.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sg::SizeAndAlignment size_align = device->calculate_resource_size_alignment(rcd);
		const sg::u64 res_size = device->CalculateResourceSize(w, h, 1, 0, rcd.format);
		seAssert(res_size == bitmap_data.size(), "expecting sizes to match");
		SharedPtr<Memory> mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Texture, size_align.size, size_align.alignment);
		terrain_texture = device->create_texture(mem, size_align.size, rcd);
		//sg::SharedPtr<sg::Buffer> terrain_buffer = device->create_buffer(mem, size_align.size, sg::BufferType::Texture, false);

		device->set_imgui_viewer_texture(terrain_texture);

		UploadHeap::Offset upload_offset = upload_heap->allocate_upload_memory(size_align.size, size_align.alignment);
		upload_heap->write_upload_memory(upload_offset, bitmap_data.data(), bitmap_data.size());

		upload_heap->upload_to_texture(terrain_texture.get(), upload_offset, bitmap_data.size());
		//upload_heap->upload_to_buffer(terrain_buffer.get(), 0, upload_offset, bitmap_data.size());
	}
}
