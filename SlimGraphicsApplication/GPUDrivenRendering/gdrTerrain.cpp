#include "gdrTerrain.h"
#include <seEngine.h>
#include <seEngineBasicFileIO.h>
#include <meshoptimizer.h>
#include <DirectXCollision.h>
#include <lodepng.h>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace gdr
{
	namespace
	{
		std::uint32_t hash(std::int32_t x, std::int32_t y, std::uint32_t seed)
		{
			std::uint32_t h = seed * 0x9E3779B9u ^ static_cast<std::uint32_t>(x) * 0x85EBCA6Bu ^ static_cast<std::uint32_t>(y) * 0xC2B2AE35u;
			h ^= h >> 16; h *= 0x7FEB352Du;
			h ^= h >> 15; h *= 0x846CA68Bu;
			h ^= h >> 16;
			return h;
		}

		// Gradient (Perlin style) noise, roughly -0.7..0.7
		float gradient_noise(float x, float y, std::uint32_t seed)
		{
			const float fx0 = std::floor(x), fy0 = std::floor(y);
			const std::int32_t x0 = static_cast<std::int32_t>(fx0), y0 = static_cast<std::int32_t>(fy0);
			const float dx = x - fx0, dy = y - fy0;
			auto corner = [&](std::int32_t ix, std::int32_t iy, float ox, float oy)
				{
					const float angle = static_cast<float>(hash(ix, iy, seed) & 0xFFFFu) * (6.28318530718f / 65536.0f);
					return std::cos(angle) * ox + std::sin(angle) * oy;
				};
			const float u = dx * dx * dx * (dx * (dx * 6.0f - 15.0f) + 10.0f);
			const float v = dy * dy * dy * (dy * (dy * 6.0f - 15.0f) + 10.0f);
			const float a = corner(x0, y0, dx, dy), b = corner(x0 + 1, y0, dx - 1.0f, dy);
			const float c = corner(x0, y0 + 1, dx, dy - 1.0f), d = corner(x0 + 1, y0 + 1, dx - 1.0f, dy - 1.0f);
			return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
		}

		// Fractal sum of 'octaves' noise layers (each rotated to hide the lattice), normalised to roughly -1..1. 'ridged' mixes in
		// folded noise, which forms sharp crests.
		float fractal_noise(float x, float y, std::uint32_t octaves, float ridged, std::uint32_t seed)
		{
			float sum = 0.0f, ridge_sum = 0.0f, amplitude = 1.0f, total = 0.0f;
			for (std::uint32_t octave = 0; octave < octaves; octave++)
			{
				const float n = gradient_noise(x, y, seed + octave * 1013u) * 1.4f;
				sum += n * amplitude;
				const float r = 1.0f - std::fabs(n);
				ridge_sum += (r * r * 2.0f - 1.0f) * amplitude;
				total += amplitude;
				amplitude *= 0.5f;
				const float rx = x * 1.6f - y * 1.2f, ry = x * 1.2f + y * 1.6f; // Rotate and scale by 2 for the next octave
				x = rx; y = ry;
			}
			return (sum + (ridge_sum - sum) * ridged) / total;
		}

		float smoothstep(float edge0, float edge1, float x)
		{
			const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-6f), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// Heights 0..1 from a greyscale PNG (16 bit preferred), sampled bilinearly at grid point (u, v) in 0..1
		struct Heightmap
		{
			std::vector<unsigned char> pixels; // 16 bit big endian greyscale
			unsigned width = 0, height = 0;

			bool load(const std::string& path)
			{
				const std::vector<uint8_t> file = se::BasicFileIO::load_file(path.c_str());
				return !file.empty() && lodepng::decode(pixels, width, height, file, LCT_GREY, 16) == 0 && width > 1 && height > 1;
			}

			float texel(unsigned x, unsigned y) const
			{
				const size_t i = (static_cast<size_t>(y) * width + x) * 2;
				return static_cast<float>((pixels[i] << 8) | pixels[i + 1]) / 65535.0f;
			}

			float sample(float u, float v) const
			{
				const float x = std::clamp(u, 0.0f, 1.0f) * (width - 1), y = std::clamp(v, 0.0f, 1.0f) * (height - 1);
				const unsigned x0 = static_cast<unsigned>(x), y0 = static_cast<unsigned>(y);
				const unsigned x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
				const float fx = x - x0, fy = y - y0;
				const float top = texel(x0, y0) + (texel(x1, y0) - texel(x0, y0)) * fx;
				const float bottom = texel(x0, y1) + (texel(x1, y1) - texel(x0, y1)) * fx;
				return top + (bottom - top) * fy;
			}
		};

		ShaderStructs::float4 colour4(const Float3& c)
		{
			return ShaderStructs::float4(c.x, c.y, c.z, 1.0f);
		}
	}

	std::string terrain_model_name(const TerrainDesc& desc)
	{
		return "@terrain/" + nlohmann::json(desc).dump();
	}

	bool is_terrain_model_name(std::string_view name)
	{
		return name.rfind("@terrain/", 0) == 0;
	}

	bool build_terrain(const TerrainDesc& desc, TerrainBuild& out)
	{
		if (!(desc.size > 0.0f) || desc.resolution == 0 || desc.resolution > 4096)
		{
			seWriteLine("Terrain: invalid size or resolution");
			return false;
		}
		const std::uint32_t quads = ((desc.resolution + GDR_TERRAIN_TILE_QUADS - 1) / GDR_TERRAIN_TILE_QUADS) * GDR_TERRAIN_TILE_QUADS;
		const std::uint32_t row = quads + 1;
		const float spacing = desc.size / static_cast<float>(quads);
		const float origin = -desc.size * 0.5f;

		Heightmap heightmap;
		if (!desc.heightmap.empty() && !heightmap.load(desc.heightmap))
		{
			seWriteLine("Terrain: cannot load 16 bit greyscale PNG heightmap %s, using noise", desc.heightmap.c_str());
			heightmap = Heightmap();
		}

		// Heights: hills from noise (or the heightmap), flattened inside the content area
		out.heights.resize(static_cast<size_t>(row) * row);
		const float frequency = 1.0f / std::max(desc.feature_size, 1e-3f);
		const bool has_flat_area = desc.flat_x_max > desc.flat_x_min && desc.flat_z_max > desc.flat_z_min;
		for (std::uint32_t gz = 0; gz < row; gz++)
		{
			for (std::uint32_t gx = 0; gx < row; gx++)
			{
				const float x = origin + gx * spacing, z = origin + gz * spacing;
				float hills;
				if (!heightmap.pixels.empty())
				{
					hills = heightmap.sample(static_cast<float>(gx) / quads, static_cast<float>(gz) / quads) * desc.height;
				}
				else
				{
					// Domain warp bends the hills into organic shapes
					const float wx = fractal_noise(x * frequency * 0.5f + 17.3f, z * frequency * 0.5f, 3, 0.0f, desc.seed + 7u);
					const float wz = fractal_noise(x * frequency * 0.5f, z * frequency * 0.5f + 41.7f, 3, 0.0f, desc.seed + 13u);
					const float n = fractal_noise(x * frequency + wx * 0.8f, z * frequency + wz * 0.8f, desc.octaves, desc.ridges, desc.seed);
					hills = desc.height * (n * 0.65f + 0.35f); // Mostly above ground, dipping below it in the lowest valleys
				}
				float height = hills;
				if (has_flat_area)
				{
					// Distance outside the flat rectangle, its edge roughened by noise so it does not look man made
					const float dx = std::max({ desc.flat_x_min - x, 0.0f, x - desc.flat_x_max });
					const float dz = std::max({ desc.flat_z_min - z, 0.0f, z - desc.flat_z_max });
					const float edge_noise = gradient_noise(x * 0.05f, z * 0.05f, desc.seed + 99u) * desc.flat_falloff * 0.5f;
					const float blend = smoothstep(0.0f, desc.flat_falloff, std::sqrt(dx * dx + dz * dz) + edge_noise);
					height = desc.flat_height + (hills - desc.flat_height) * blend;
				}
				out.heights[static_cast<size_t>(gz) * row + gx] = height;
			}
		}

		const float water_level = desc.water ? desc.water_level : -FLT_MAX;
		float max_height = -FLT_MAX;
		for (float h : out.heights) max_height = std::max(max_height, h);

		ShaderStructs::TerrainData& d = out.data;
		d = {};
		d.origin_x = origin;
		d.origin_z = origin;
		d.spacing = spacing;
		d.water_level = water_level;
		d.height_offset = 0;
		d.quads = quads;
		d.snow_height = desc.snow_height;
		d.max_height = max_height;
		d.grass_colour = colour4(desc.grass_colour);
		d.rock_colour = colour4(desc.rock_colour);
		d.sand_colour = colour4(desc.sand_colour);
		d.snow_colour = colour4(desc.snow_colour);
		d.water_colour = colour4(desc.water_colour);

		// Tile meshlets: positions are only built here (transiently) for each tile's culling bounds and normal cone
		unsigned int tile_vertices[GDR_TERRAIN_TILE_VERTICES];
		unsigned char tile_triangles[GDR_TERRAIN_TILE_TRIANGLES * 3];
		for (std::uint32_t i = 0; i < GDR_TERRAIN_TILE_VERTICES; i++) tile_vertices[i] = i;
		for (std::uint32_t t = 0; t < GDR_TERRAIN_TILE_TRIANGLES; t++)
		{
			std::uint32_t indices[3];
			terrain_tile_triangle(t, indices);
			for (int k = 0; k < 3; k++) tile_triangles[t * 3 + k] = static_cast<unsigned char>(indices[k]);
		}

		const std::uint32_t tiles_per_side = quads / GDR_TERRAIN_TILE_QUADS;
		out.tiles.clear();
		out.tiles.reserve(static_cast<size_t>(tiles_per_side) * tiles_per_side);
		out.bounds_min = { FLT_MAX, FLT_MAX, FLT_MAX };
		out.bounds_max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		DirectX::XMFLOAT3 positions[GDR_TERRAIN_TILE_VERTICES];
		for (std::uint32_t tz = 0; tz < tiles_per_side; tz++)
		{
			for (std::uint32_t tx = 0; tx < tiles_per_side; tx++)
			{
				const std::uint32_t first_x = tx * GDR_TERRAIN_TILE_QUADS, first_z = tz * GDR_TERRAIN_TILE_QUADS;
				DirectX::XMFLOAT3 tile_min = { FLT_MAX, FLT_MAX, FLT_MAX }, tile_max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
				for (std::uint32_t i = 0; i < GDR_TERRAIN_TILE_VERTICES; i++)
				{
					const std::uint32_t gx = first_x + i % (GDR_TERRAIN_TILE_QUADS + 1), gz = first_z + i / (GDR_TERRAIN_TILE_QUADS + 1);
					const float h = std::max(out.heights[static_cast<size_t>(gz) * row + gx], water_level);
					positions[i] = { origin + gx * spacing, h, origin + gz * spacing };
					tile_min = { std::min(tile_min.x, positions[i].x), std::min(tile_min.y, h), std::min(tile_min.z, positions[i].z) };
					tile_max = { std::max(tile_max.x, positions[i].x), std::max(tile_max.y, h), std::max(tile_max.z, positions[i].z) };
				}
				out.bounds_min = { std::min(out.bounds_min.x, tile_min.x), std::min(out.bounds_min.y, tile_min.y), std::min(out.bounds_min.z, tile_min.z) };
				out.bounds_max = { std::max(out.bounds_max.x, tile_max.x), std::max(out.bounds_max.y, tile_max.y), std::max(out.bounds_max.z, tile_max.z) };

				const meshopt_Bounds bounds = meshopt_computeMeshletBounds(tile_vertices, tile_triangles, GDR_TERRAIN_TILE_TRIANGLES,
					&positions[0].x, GDR_TERRAIN_TILE_VERTICES, sizeof(DirectX::XMFLOAT3));

				ShaderStructs::MeshletData& tile = out.tiles.emplace_back();
				tile = {};
				tile.meshlet_type = GDR_MESHLET_TYPE_TERRAIN;
				tile.vertex_count = GDR_TERRAIN_TILE_VERTICES;
				tile.triangle_count = GDR_TERRAIN_TILE_TRIANGLES;
				tile.terrain_tile_x = first_x;
				tile.terrain_tile_z = first_z;
				tile.spherepos_xyz_radius_w = ShaderStructs::float4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
				tile.cone_apex_xyz = ShaderStructs::float4(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2], 0.0f);
				tile.cone_axis_xyz_cone_cutoff_w = ShaderStructs::float4(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], bounds.cone_cutoff);
				char* cone_s8 = reinterpret_cast<char*>(&tile.cone_axis_s8xyz_cone_cutoff_s8w);
				cone_s8[0] = bounds.cone_axis_s8[0]; cone_s8[1] = bounds.cone_axis_s8[1]; cone_s8[2] = bounds.cone_axis_s8[2]; cone_s8[3] = bounds.cone_cutoff_s8;

				const DirectX::BoundingBox aabb(DirectX::XMFLOAT3((tile_min.x + tile_max.x) * 0.5f, (tile_min.y + tile_max.y) * 0.5f, (tile_min.z + tile_max.z) * 0.5f),
					DirectX::XMFLOAT3((tile_max.x - tile_min.x) * 0.5f, (tile_max.y - tile_min.y) * 0.5f, (tile_max.z - tile_min.z) * 0.5f));
				tile.aabb_center = ShaderStructs::float4(aabb.Center.x, aabb.Center.y, aabb.Center.z, 0.0f);
				tile.aabb_extents = ShaderStructs::float4(aabb.Extents.x, aabb.Extents.y, aabb.Extents.z, 0.0f);
				tile.obb_center = tile.aabb_center;
				tile.obb_extents = tile.aabb_extents;
				tile.obb_orientation = ShaderStructs::float4(0.0f, 0.0f, 0.0f, 1.0f);
			}
		}
		return true;
	}
}
