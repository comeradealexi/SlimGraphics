#pragma once
#include <DirectXMath.h>
#include <string>
#include <string_view>
#include <vector>
#include "gdrJSON.h"
#include "gdrShaderStructures.h"

namespace gdr
{
	// Procedural terrain for the GPU driven renderer. A terrain is a model whose meshlets are tiles of
	// GDR_TERRAIN_TILE_QUADS x GDR_TERRAIN_TILE_QUADS quads. Only the height grid is stored (generated when the scene loads,
	// never written to disk); the mesh shader builds each tile's vertices, normals and triangles from it every frame, so
	// terrain goes through the same culling, sorting and single indirect mesh dispatch as every other model.
	struct TerrainBuild
	{
		ShaderStructs::TerrainData data = {};			// height_offset is relative to 'heights' (the caller rebases it)
		std::vector<float> heights;						// (quads + 1)^2 heights, row major (z rows of x)
		std::vector<ShaderStructs::MeshletData> tiles;	// One meshlet per tile with its culling data; the caller sets the
														// material and terrain indices
		DirectX::XMFLOAT3 bounds_min = {};
		DirectX::XMFLOAT3 bounds_max = {};
	};

	// Generates the height grid and tile meshlets of a terrain. Returns false if the description is unusable.
	bool build_terrain(const TerrainDesc& desc, TerrainBuild& out);

	// Terrains are keyed like models: nodes with identical terrain descriptions share one terrain
	std::string terrain_model_name(const TerrainDesc& desc);
	bool is_terrain_model_name(std::string_view name);

	// Vertex indices (row major within the tile's 8 x 8 vertices) of a tile triangle. Must match TerrainTileTriangle in
	// gdrShaderCompute.hlsl: quads alternate their diagonal in a checkerboard, and triangles wind clockwise seen from +y.
	inline void terrain_tile_triangle(std::uint32_t triangle, std::uint32_t out_indices[3])
	{
		const std::uint32_t quad = triangle / 2;
		const std::uint32_t qx = quad % GDR_TERRAIN_TILE_QUADS;
		const std::uint32_t qz = quad / GDR_TERRAIN_TILE_QUADS;
		const std::uint32_t row = GDR_TERRAIN_TILE_QUADS + 1;
		const std::uint32_t v00 = qz * row + qx, v10 = v00 + 1, v01 = v00 + row, v11 = v01 + 1;
		const bool second = (triangle & 1) != 0;
		if (((qx + qz) & 1) == 0)
		{
			out_indices[0] = second ? v10 : v00; out_indices[1] = v01; out_indices[2] = second ? v11 : v10;
		}
		else
		{
			out_indices[0] = v00; out_indices[1] = second ? v11 : v01; out_indices[2] = second ? v10 : v11;
		}
	}
}
