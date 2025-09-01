#include "DebugDraw.h"
#include <seEngineBasicFileIO.h>
#include <imgui.h>

using namespace sg;

DebugDraw::DebugDraw(sg::Device& device)
{
	const sg::u32 max_vertex_size = 3 * MAX_TRIANGLES * sizeof(VertexFormat);
	const sg::u32 max_index_size = 9 * MAX_TRIANGLES * sizeof(uint32_t);
	const sg::u32 total_byte_size = max_vertex_size + max_index_size;

	for (auto& upload_buffer : upload_buffers)
	{
		SharedPtr<Memory> upload_heap = device.allocate_memory(MemoryType::Upload, MemorySubType::None, total_byte_size);
		upload_buffer = device.create_buffer(upload_heap, total_byte_size, BufferType::Upload, false);
	}

	// VB
	{
		SharedPtr<Memory> vb_mem = device.allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, max_vertex_size);
		gpu_vertex_buffer = device.create_buffer(vb_mem, max_vertex_size, BufferType::Vertex, false);
		gpu_vertex_buffer_view = device.create_vertex_buffer_view(gpu_vertex_buffer, 0, max_vertex_size, sizeof(VertexFormat));
	}

	// IB
	{
		SharedPtr<Memory> ib_mem = device.allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, max_index_size);
		gpu_index_buffer = device.create_buffer(ib_mem, max_index_size, BufferType::Index, false);
		gpu_index_buffer_view = device.create_index_buffer_view(gpu_index_buffer, 0, max_index_size, DXGI_FORMAT_R16_UINT);
	}

	// Shaders
	{
		std::vector<uint8_t> vertex_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\DebugWireframeShader_VertexShader.PC_DXC");
		std::vector<uint8_t> pixel_data = se::BasicFileIO::LoadFile("ShaderBin_Debug\\DebugWireframeShader_PixelShader.PC_DXC");
		sg::SharedPtr<sg::VertexShader> shader_vertex = device.create_vertex_shader(vertex_data);
		sg::SharedPtr<sg::PixelShader> shader_pixel = device.create_pixel_shader(pixel_data);

		sg::BindingDesc pipeline_binding_desc = {}; 
		pipeline_binding_desc.cbv_binding_count = 1;

		sg::PipelineDesc::Graphics pipeline_desc;
		pipeline_desc.input_layout = make_input_layout();
		pipeline_desc.vertex_shader = shader_vertex;
		pipeline_desc.pixel_shader = shader_pixel;
		pipeline_desc.render_target_count = 1;
		pipeline_desc.render_target_format_list[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pipeline_desc.depth_stencil_desc.depth_enable = true;
		pipeline_desc.depth_stencil_desc.depth_write = false;
		pipeline_desc.rasterizer_desc.fill_mode = Rasterizer::FillMode::Wireframe;
		pipeline_desc.rasterizer_desc.cull_mode = Rasterizer::CullMode::None;
		pipeline_no_depth = device.create_pipeline(pipeline_desc, pipeline_binding_desc);
		seAssert(pipeline_no_depth != nullptr, "Failed to create pipeline");

		pipeline_desc.depth_stencil_format = DXGI_FORMAT_D32_FLOAT;
		pipeline_desc.depth_stencil_desc.depth_enable = true;
		pipeline_depth = device.create_pipeline(pipeline_desc, pipeline_binding_desc);
		seAssert(pipeline_depth != nullptr, "Failed to pipeline");
	}
}

void DebugDraw::DrawAABB(ColourRGBA colour, DirectX::XMFLOAT3 centre, const DirectX::XMFLOAT3& min_extent, const DirectX::XMFLOAT3& max_extent)
{
	if (!IsEnabled()) return;
}

void DebugDraw::DrawAABB(ColourRGBA colour, DirectX::XMFLOAT3 centre, const DirectX::BoundingBox aabb)
{
	if (!IsEnabled()) return;

	DirectX::VertexCollection vc;
	DirectX::IndexCollection ic;	

	DirectX::XMFLOAT3 corners[8];
	aabb.GetCorners(corners);

	for (auto& v : corners)
	{
		vc.emplace_back().position = v;
	}

	// From the DXMath AABB Layout
	//static const g_BoxOffset[8] =
	//{ 
	//	{ { { -1.0f, -1.0f,  1.0f, 0.0f } } },	0
	//	{ { {  1.0f, -1.0f,  1.0f, 0.0f } } },	1
	//	{ { {  1.0f,  1.0f,  1.0f, 0.0f } } },	2
	//	{ { { -1.0f,  1.0f,  1.0f, 0.0f } } },	3
	//	{ { { -1.0f, -1.0f, -1.0f, 0.0f } } },	4
	//	{ { {  1.0f, -1.0f, -1.0f, 0.0f } } },	5
	//	{ { {  1.0f,  1.0f, -1.0f, 0.0f } } },	6
	//	{ { { -1.0f,  1.0f, -1.0f, 0.0f } } },	7
	//};

	auto add_face = [&](int16_t a, int16_t b, int16_t c, int16_t d)
	{
		ic.push_back(a); ic.push_back(b); ic.push_back(c);
		ic.push_back(b); ic.push_back(c); ic.push_back(d);
	};

	// Z
	add_face(0, 1, 2, 3);
	add_face(4, 5, 6, 7);

	// X
	add_face(0, 3, 4, 7);
	add_face(1, 2, 5, 6);

	// Y
	add_face(0, 1, 4, 5);
	add_face(2, 3, 6, 7);

	FinaliseDraw(colour, centre, vc, ic);
}

void DebugDraw::DrawSphere(ColourRGBA colour, DirectX::XMFLOAT3 centre, float diameter /*= 1.0f*/, size_t tessellation /*= 3*/)
{
	if (!IsEnabled()) return;

	DirectX::VertexCollection v;
	DirectX::IndexCollection i;
	DirectX::ComputeSphere(v, i, diameter, tessellation, true, false);

	FinaliseDraw(colour, centre, v, i);
}

void DebugDraw::DrawGeoSphere(ColourRGBA colour, DirectX::XMFLOAT3 centre, float diameter /*= 1.0f*/, size_t tessellation /*= 3*/)
{
	if (!IsEnabled()) return;

	DirectX::VertexCollection v;
	DirectX::IndexCollection i;
	DirectX::ComputeGeoSphere(v, i, diameter, tessellation, true);

	FinaliseDraw(colour, centre, v, i);
}


void DebugDraw::FinaliseDraw(ColourRGBA colour, DirectX::XMFLOAT3 centre, DirectX::VertexCollection& v, DirectX::IndexCollection& i)
{
	DirectX::XMMATRIX transform = DirectX::XMMatrixTranslation(centre.x, centre.y, centre.z);

	draw_list.push_back({ (sg::u32)i.size(), (sg::u32)indices.size(), (sg::u32)vertices.size() });

	indices.insert(indices.end(), i.begin(), i.end());

	for (size_t i = 0; i < v.size(); i++)
	{
		const DirectX::VertexPositionNormalTexture& v1 = v[i];
		VertexFormat v2;
		DirectX::XMStoreFloat3(&v2.position, XMVector3Transform(DirectX::XMLoadFloat3(&v1.position), transform));
		v2.colour = colour;
		vertices.push_back(v2);
	}
}


void DebugDraw::Render(sg::CommandList& command_list, sg::ConstantBufferView& cbv_camera)
{
	if (draw_list.size())
	{
		sg::SharedPtr<sg::Buffer>& upload = upload_buffers[buffer_index];

		const sg::u32 vertex_size = vertices.size() * sizeof(vertices[0]);
		const sg::u32 index_size = indices.size() * sizeof(indices[0]);
		upload->write_memory(0, vertices.data(), vertex_size);
		upload->write_memory(vertex_size, indices.data(), index_size);
		command_list.copy_buffer_to_buffer(vertex_size, gpu_vertex_buffer.get(), 0, upload.get(), 0);
		command_list.copy_buffer_to_buffer(index_size, gpu_index_buffer.get(), 0, upload.get(), vertex_size);

		command_list.set_pipeline(pipeline_no_depth.get());

		command_list.bind_vertex_buffer(gpu_vertex_buffer_view);
		command_list.bind_index_buffer(gpu_index_buffer_view);

		Binding b;
		b.cbv_binding_count = 1;
		b.set_cbv(cbv_camera, 0);
		command_list.bind(b, PipelineType::Geometry);

		for (auto d : draw_list)
		{
			command_list.draw_indexed_instanced(d.index_count, 1, d.index_offset, d.vertex_offset, 0);
		}
		draw_list.clear();
		vertices.clear();
		indices.clear();

		buffer_index++;
		buffer_index = buffer_index % UPLOAD_BUFFER_COUNT;
	}
}

void DebugDraw::Update()
{
	if (ImGui::CollapsingHeader("Debug Draw"))
	{
		ImGui::Checkbox("Enabled", &options.enabled);
	}
}
