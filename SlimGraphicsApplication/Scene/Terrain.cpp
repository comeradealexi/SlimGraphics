#include "Terrain.h"
#include <lodepng.h>

// https://learnopengl.com/Guest-Articles/2021/Tessellation/Height-map

using namespace sg;

Terrain::Terrain(SharedPtr<Device>& _device) : device(_device)
{
	height_textures_list.push_back("");
}
