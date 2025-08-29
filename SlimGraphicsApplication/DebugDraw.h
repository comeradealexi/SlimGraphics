#pragma once
#include <sgPlatformInclude.h>
#include <DirectXCollision.h>
#include <Geometry.h>

class DebugDraw
{
public:
	union ColourRGBA
	{
		// Unset
		ColourRGBA() { }

		// 0 to 255
		ColourRGBA(sg::u8 red, sg::u8 green, sg::u8 blue, sg::u8 alpha = 255) 
		{	
			r = red;
			g = green; 
			b = blue;
			a = alpha;
		}

		union
		{
			sg::u8 r;
			sg::u8 g;
			sg::u8 b;
			sg::u8 a;
		};
		sg::u32 rgba;
	};
	static_assert(sizeof(ColourRGBA) == 4);

	DebugDraw(sg::Device& device);

	bool IsEnabled() const { return options.enabled; }

	void DrawAABB(ColourRGBA colour, DirectX::XMFLOAT3 centre, const DirectX::XMFLOAT3& min_extent, const DirectX::XMFLOAT3& max_extent);
	void DrawAABB(ColourRGBA colour, DirectX::XMFLOAT3 centre, const DirectX::BoundingBox aabb);

	void DrawSphere(ColourRGBA colour, DirectX::XMFLOAT3 centre, float diameter = 1.0f, size_t tessellation = 3);	
	void DrawGeoSphere(ColourRGBA colour, DirectX::XMFLOAT3 centre, float diameter = 1.0f, size_t tessellation = 3);
	void Render(sg::CommandList& command_list, sg::ConstantBufferView& cbv_camera);

	static inline sg::InputLayout::Desc make_input_layout(sg::u32* out_stride = nullptr)
	{
		using namespace sg;
		InputLayout::Desc d = {};

		sg::u32 size_offset = 0;
		d.elements[d.num_elements++] = InputLayout::ElementDesc({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, size_offset, InputLayout::InputClassification::PerVertex, 0 });
		size_offset += sizeof(VertexFormat::position);

		d.elements[d.num_elements++] = InputLayout::ElementDesc({ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, size_offset, InputLayout::InputClassification::PerVertex, 0 });
		size_offset += sizeof(VertexFormat::colour);

		if (out_stride)
			*out_stride = size_offset;

		return d;
	}
private:
	void FinaliseDraw(ColourRGBA colour, DirectX::XMFLOAT3 centre, DirectX::VertexCollection& v, DirectX::IndexCollection& i);

private:
	static constexpr sg::u32 MAX_TRIANGLES = 100000;
	static constexpr sg::u32 UPLOAD_BUFFER_COUNT = 3;
	sg::u32 buffer_index = 0;
	std::array<sg::SharedPtr<sg::Buffer>, UPLOAD_BUFFER_COUNT> upload_buffers;
	sg::SharedPtr<sg::Buffer> gpu_vertex_buffer;
	sg::SharedPtr<sg::Buffer> gpu_index_buffer;
	sg::SharedPtr<sg::Pipeline> pipeline_depth;
	sg::SharedPtr<sg::Pipeline> pipeline_no_depth;

	struct VertexFormat
	{
		DirectX::XMFLOAT3 position;
		ColourRGBA colour;
	};

	std::vector<VertexFormat> vertices;
	DirectX::IndexCollection indices;

	sg::VertexBufferView gpu_vertex_buffer_view;
	sg::IndexBufferView gpu_index_buffer_view;

	struct Draw
	{
		sg::u32 index_count;
		sg::u32 index_offset;
		sg::u32 vertex_offset;
	};
	std::vector<Draw> draw_list;

	struct Options
	{
		bool enabled = false;
	}options;
};