
#include "MagnifyingGlass.h"
#include "sgUtils.h"

using namespace sg;

MagnifyingGlass::MagnifyingGlass(sg::SharedPtr<sg::Device>& _device) : device(_device)
{
	ResourceCreateDesc rcd(target_size, target_size, DXGI_FORMAT_R8G8B8A8_UNORM, ResourceUsageFlags::RenderTarget);
	render_texture = create_texture(*device, rcd);
	render_target_view = device->create_render_target_view(render_texture);
}

void MagnifyingGlass::Update(float delta_time, float total_time, const Camera& camera, DebugDraw& debug_draw)
{

}

void MagnifyingGlass::Render(sg::CommandList& command_list, SharedPtr<ShaderResourceView> read_texture, SimpleLinearConstantBuffer& cbuffer)
{

}

