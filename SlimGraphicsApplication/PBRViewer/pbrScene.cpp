#include "pbrScene.h"
#include <sgUtils.h>
#include <Utils.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>
#include <meshoptimizer.h>
#include "stb_image.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <unordered_map>

using namespace DirectX;

namespace pbr
{
	const std::vector<ScenePreset>& scene_presets()
	{
		// Cameras and suns chosen to show each scene's materials and lighting. Positions are in metres after 'scale'.
		static const std::vector<ScenePreset> presets =
		{
			{ "Sponza", "../SlimGraphicsAssets/SponzaGL/sponza.obj", 0.08f, { 9.0f, 1.7f, -0.6f }, { -6.0f, 4.0f, 0.4f }, 72.0f, 25.0f, false, "PBRViewer/CameraTracks/Sponza.json" },
			{ "Bistro Exterior", "../SlimGraphicsAssets/Bistro_v5_2/BistroExterior.fbx", 1.0f, {}, {}, 35.0f, 230.0f, true, "PBRViewer/CameraTracks/BistroExterior.json" },
			{ "Bistro Exterior (Dusk)", "../SlimGraphicsAssets/Bistro_v5_2/BistroExterior.fbx", 1.0f, {}, {}, -4.0f, 230.0f, true, "PBRViewer/CameraTracks/BistroExterior.json" },
			{ "Bistro Interior", "../SlimGraphicsAssets/Bistro_v5_2/BistroInterior_Wine.fbx", 1.0f, {}, {}, 30.0f, 300.0f, true, "PBRViewer/CameraTracks/BistroInterior.json" },
			{ "Sun Temple", "../SlimGraphicsAssets/UE4SunTemple_v4/SunTemple/SunTemple.fbx", 1.0f, {}, {}, 60.0f, 20.0f, true, "PBRViewer/CameraTracks/SunTemple.json" },
			{ "Emerald Square", "../SlimGraphicsAssets/EmeraldSquare_v4_1/EmeraldSquare_Day.fbx", 1.0f, {}, {}, 40.0f, 140.0f, true, "PBRViewer/CameraTracks/EmeraldSquare.json" },
			{ "Damaged Helmet", "../SlimGraphicsAssets/Khronos/DamagedHelmet/glTF/DamagedHelmet.gltf", 1.0f, { 0.0f, 0.2f, -3.0f }, { 0.0f, 0.0f, 0.0f }, 35.0f, 240.0f, false, nullptr },
			{ "Sci-Fi Helmet", "../SlimGraphicsAssets/Khronos/SciFiHelmet/SciFiHelmet.gltf", 1.0f, { 0.0f, 0.5f, -3.5f }, { 0.0f, 0.0f, 0.0f }, 35.0f, 240.0f, false, nullptr },
			{ "Metal Rough Spheres", "../SlimGraphicsAssets/Khronos/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf", 1.0f, { 0.0f, 0.0f, -14.0f }, { 0.0f, 0.0f, 0.0f }, 40.0f, 30.0f, false, nullptr },
			{ "PICA PICA Robot", "../SlimGraphicsAssets/Khronos/pica_pica_robot/scene.gltf", 1.0f, { 0.0f, 1.2f, -4.0f }, { 0.0f, 0.9f, 0.0f }, 40.0f, 240.0f, false, nullptr },
		};
		return presets;
	}

	const ScenePreset* find_scene_preset(const std::string& name)
	{
		for (const ScenePreset& preset : scene_presets())
		{
			if (name == preset.name)
			{
				return &preset;
			}
		}
		return nullptr;
	}

	void Bounds::extend(const XMFLOAT3& p)
	{
		min = { std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z) };
		max = { std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z) };
	}

	float Bounds::radius() const
	{
		const XMFLOAT3 s = size();
		return 0.5f * std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
	}

	Bounds Bounds::transformed(FXMMATRIX world) const
	{
		Bounds out;
		for (int c = 0; c < 8; c++)
		{
			XMFLOAT3 p;
			XMStoreFloat3(&p, XMVector3TransformCoord(XMVectorSet(c & 1 ? max.x : min.x, c & 2 ? max.y : min.y, c & 4 ? max.z : min.z, 1.0f), world));
			out.extend(p);
		}
		return out;
	}

	sg::SharedPtr<sg::Buffer> create_padded_buffer(sg::Device& device, size_t bytes, sg::BufferType type, bool unordered_access)
	{
		return sg::create_buffer(device, static_cast<sg::u32>(sg::AlignUp<size_t>(std::max<size_t>(bytes, 1), 256)), type, unordered_access);
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// Import (worker thread)

	struct Scene::Texture
	{
		std::string key;
		bool srgb = false;
		bool dds = false;
		std::vector<uint8_t> data;			// DDS file, or RGBA8 mips one after another
		sg::u32 width = 0;
		sg::u32 height = 0;
		sg::u32 mip_count = 0;
		bool may_have_alpha = true;
	};

	struct Scene::ImportedScene
	{
		std::vector<gpu::Vertex> vertices;
		std::vector<sg::u32> indices;
		std::vector<gpu::Meshlet> meshlets;
		std::vector<sg::u32> meshlet_vertices;
		std::vector<sg::u32> meshlet_triangles;
		std::vector<gpu::Mesh> meshes;
		std::vector<gpu::Material> materials;	// Texture fields index 'textures' until uploaded
		std::vector<bool> material_alpha_candidate;
		std::vector<Texture> textures;
		std::vector<gpu::Instance> instances;
		std::vector<gpu::EmissiveTriangle> emissive_triangles;	// Light sampling alias table
		double emissive_weight = 0.0;
		Bounds bounds;
		bool has_camera = false;
		XMFLOAT3 camera_position = { 0, 0, 0 };
		XMFLOAT3 camera_target = { 0, 0, 1 };
		std::string error;
		float seconds = 0.0f;
	};

	namespace
	{
		constexpr sg::u64 kUploadBudget = 64ull << 20;		// Bytes of scene data uploaded per frame (the upload heap is per frame)
		constexpr sg::u32 kBlasBuildsPerFrame = 256;
		constexpr sg::u64 kBlasPoolBytes = 64ull << 20;

		float srgb_to_linear(float c)
		{
			return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}

		float linear_to_srgb(float c)
		{
			return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
		}

		XMFLOAT3 linear_colour(const aiColor4D& c, bool srgb)
		{
			return srgb ? XMFLOAT3(srgb_to_linear(c.r), srgb_to_linear(c.g), srgb_to_linear(c.b)) : XMFLOAT3(c.r, c.g, c.b);
		}

		float luminance(const XMFLOAT3& c)
		{
			return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
		}

		std::string lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		bool read_file(const std::string& path, std::vector<uint8_t>& out)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
			{
				return false;
			}
			const std::streamsize size = file.tellg();
			file.seekg(0, std::ios::beg);
			out.resize(static_cast<size_t>(size));
			return size == 0 || static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
		}

		// Texture paths in model files are often relative to another folder or absolute; try the usual places, and the same
		// texture converted to DDS (common for FBX scenes authored with PNG / TGA references)
		std::string resolve_texture_path(const std::string& model_path, std::string texture_path)
		{
			std::replace(texture_path.begin(), texture_path.end(), '\\', '/');
			const std::filesystem::path model_dir = std::filesystem::path(model_path).parent_path();
			const std::filesystem::path file_name = std::filesystem::path(texture_path).filename();
			for (const std::filesystem::path& candidate : { model_dir / texture_path, model_dir / file_name, model_dir / "Textures" / file_name, model_dir / "textures" / file_name })
			{
				for (const std::filesystem::path& file : { candidate, std::filesystem::path(candidate).replace_extension(".dds") })
				{
					std::error_code ec;
					if (std::filesystem::is_regular_file(file, ec))
					{
						return file.string();
					}
				}
			}
			return {};
		}

		// Whether a DDS file's pixel format can hold alpha (compressed formats are analysed on the GPU later)
		bool dds_may_have_alpha(const std::vector<uint8_t>& file)
		{
			if (file.size() < 128 || memcmp(file.data(), "DDS ", 4) != 0)
			{
				return false;
			}
			auto read_u32 = [&](size_t offset) { uint32_t v; memcpy(&v, &file[offset], 4); return v; };
			auto four_cc = [](const char* s) { return (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24); };
			const uint32_t pixel_format_flags = read_u32(80);
			if ((pixel_format_flags & 0x4) == 0) // DDPF_FOURCC
			{
				return (pixel_format_flags & 0x1) != 0; // DDPF_ALPHAPIXELS
			}
			const uint32_t code = read_u32(84);
			if (code == four_cc("DX10") && file.size() >= 148)
			{
				switch (read_u32(128))
				{
				case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
				case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8G8_UNORM:
					return false;
				default:
					return true;
				}
			}
			return code == four_cc("DXT1") || code == four_cc("DXT2") || code == four_cc("DXT3") || code == four_cc("DXT4") || code == four_cc("DXT5");
		}

		bool is_srgb_format(DXGI_FORMAT format)
		{
			return format == DXGI_FORMAT_BC1_UNORM_SRGB || format == DXGI_FORMAT_BC2_UNORM_SRGB || format == DXGI_FORMAT_BC3_UNORM_SRGB ||
				format == DXGI_FORMAT_BC7_UNORM_SRGB || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		}

		// RGBA8 mip chain with box filtering; colour (sRGB) textures are averaged in linear space
		void build_mips(const uint8_t* pixels, sg::u32 width, sg::u32 height, bool srgb, std::vector<uint8_t>& out, sg::u32& mip_count)
		{
			static const std::vector<float> to_linear = []
				{
					std::vector<float> table(256);
					for (int i = 0; i < 256; i++) table[i] = srgb_to_linear(i / 255.0f);
					return table;
				}();
			out.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
			mip_count = 1;
			size_t source = 0;
			for (sg::u32 w = width, h = height; w > 1 || h > 1; mip_count++)
			{
				const sg::u32 nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
				const size_t destination = out.size();
				out.resize(destination + static_cast<size_t>(nw) * nh * 4);
				for (sg::u32 y = 0; y < nh; y++)
				{
					for (sg::u32 x = 0; x < nw; x++)
					{
						float sum[4] = {};
						for (sg::u32 tap = 0; tap < 4; tap++)
						{
							const sg::u32 sx = std::min(x * 2 + (tap & 1), w - 1), sy = std::min(y * 2 + (tap >> 1), h - 1);
							const uint8_t* p = &out[source + (static_cast<size_t>(sy) * w + sx) * 4];
							for (int c = 0; c < 4; c++) sum[c] += (srgb && c < 3) ? to_linear[p[c]] : p[c] / 255.0f;
						}
						uint8_t* d = &out[destination + (static_cast<size_t>(y) * nw + x) * 4];
						for (int c = 0; c < 4; c++)
						{
							const float v = (srgb && c < 3) ? linear_to_srgb(sum[c] * 0.25f) : sum[c] * 0.25f;
							d[c] = static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
						}
					}
				}
				source = destination;
				w = nw;
				h = nh;
			}
		}

		XMMATRIX to_xm(const aiMatrix4x4& m)
		{
			// Assimp matrices are row major for column vectors; DirectXMath uses row vectors, so transpose
			return XMMatrixTranspose(XMMATRIX(&m.a1));
		}

		gpu::Material default_material()
		{
			gpu::Material m = {};
			m.base_colour = { 1.0f, 1.0f, 1.0f, 1.0f };
			m.alpha_cutoff = 0.5f;
			m.roughness = 0.5f;
			m.normal_scale = 1.0f;
			m.occlusion_strength = 1.0f;
			m.base_colour_texture = m.normal_texture = m.metal_rough_texture = m.emissive_texture = m.occlusion_texture = m.opacity_texture = PBR_NO_TEXTURE;
			return m;
		}

		// Vose's alias method: each column keeps its own entry with 'probability', else its alias
		void build_alias_table(const std::vector<double>& weights, double total, std::vector<gpu::EmissiveTriangle>& table)
		{
			const size_t count = weights.size();
			std::vector<double> scaled(count);
			std::vector<sg::u32> below, above; // Columns under / over the average weight
			for (size_t i = 0; i < count; i++)
			{
				scaled[i] = weights[i] * count / total;
				(scaled[i] < 1.0 ? below : above).push_back(static_cast<sg::u32>(i));
				table[i].probability = 1.0f;
				table[i].alias = static_cast<sg::u32>(i);
			}
			while (!below.empty() && !above.empty())
			{
				const sg::u32 small_column = below.back();
				const sg::u32 large_column = above.back();
				below.pop_back();
				table[small_column].probability = static_cast<float>(scaled[small_column]);
				table[small_column].alias = large_column;
				scaled[large_column] -= 1.0 - scaled[small_column];
				if (scaled[large_column] < 1.0)
				{
					above.pop_back();
					below.push_back(large_column);
				}
			}
		}
	}

	// Converts an assimp scene into the viewer's buffers, one stage per method
	struct Scene::Importer
	{
		const aiScene& scene;
		const std::string& path;
		const float scale;
		const bool specular_is_orm;
		const bool gltf;
		ImportedScene& out;
		std::unordered_map<std::string, sg::u32> texture_lookup;	// Deduplicated by file and colour space
		std::vector<sg::u32> mesh_lookup;							// Assimp mesh -> mesh (~0u: skipped)

		// The material's texture of this type (decoded once per file and colour space), or PBR_NO_TEXTURE
		sg::u32 texture(const aiMaterial& material, aiTextureType type, bool srgb)
		{
			aiString texture_path;
			if (material.GetTexture(type, 0, &texture_path) != aiReturn_SUCCESS || texture_path.length == 0)
			{
				return PBR_NO_TEXTURE;
			}
			const aiTexture* embedded = scene.GetEmbeddedTexture(texture_path.C_Str());
			const std::string file = embedded ? std::string() : resolve_texture_path(path, texture_path.C_Str());
			const std::string name = file.empty() ? texture_path.C_Str() : file;
			if (!embedded && file.empty())
			{
				seWriteLine("PBR: texture not found: %s", texture_path.C_Str());
				return PBR_NO_TEXTURE;
			}
			const std::string key = (embedded ? path + "#" + texture_path.C_Str() : file) + (srgb ? "|srgb" : "|linear");
			if (auto it = texture_lookup.find(key); it != texture_lookup.end())
			{
				return it->second;
			}

			Texture t;
			t.key = key;
			t.srgb = srgb;
			std::vector<uint8_t> encoded;
			if (embedded && embedded->mHeight != 0)
			{
				// Raw BGRA8 texels
				const size_t texels = static_cast<size_t>(embedded->mWidth) * embedded->mHeight;
				std::vector<uint8_t> rgba(texels * 4);
				for (size_t i = 0; i < texels; i++)
				{
					const aiTexel& texel = embedded->pcData[i];
					rgba[i * 4 + 0] = texel.r; rgba[i * 4 + 1] = texel.g; rgba[i * 4 + 2] = texel.b; rgba[i * 4 + 3] = texel.a;
				}
				t.width = embedded->mWidth;
				t.height = embedded->mHeight;
				build_mips(rgba.data(), t.width, t.height, srgb, t.data, t.mip_count);
			}
			else
			{
				if (embedded)
				{
					const uint8_t* data = reinterpret_cast<const uint8_t*>(embedded->pcData);
					encoded.assign(data, data + embedded->mWidth);
				}
				else if (!read_file(file, encoded))
				{
					seWriteLine("PBR: cannot read texture %s", file.c_str());
					return PBR_NO_TEXTURE;
				}
				if (encoded.size() >= 4 && memcmp(encoded.data(), "DDS ", 4) == 0)
				{
					t.dds = true;
					t.may_have_alpha = dds_may_have_alpha(encoded);
					t.data = std::move(encoded);
				}
				else
				{
					int w = 0, h = 0, channels = 0;
					stbi_uc* pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &w, &h, &channels, 4);
					if (pixels == nullptr)
					{
						seWriteLine("PBR: cannot decode texture %s", name.c_str());
						return PBR_NO_TEXTURE;
					}
					t.width = static_cast<sg::u32>(w);
					t.height = static_cast<sg::u32>(h);
					t.may_have_alpha = channels == 4 || channels == 2;
					build_mips(pixels, t.width, t.height, srgb, t.data, t.mip_count);
					stbi_image_free(pixels);
				}
			}
			const sg::u32 index = static_cast<sg::u32>(out.textures.size());
			out.textures.push_back(std::move(t));
			texture_lookup.emplace(key, index);
			return index;
		}

		// The first of these texture types the material has
		sg::u32 first_texture(const aiMaterial& material, std::initializer_list<aiTextureType> types, bool srgb)
		{
			for (aiTextureType type : types)
			{
				const sg::u32 index = texture(material, type, srgb);
				if (index != PBR_NO_TEXTURE)
				{
					return index;
				}
			}
			return PBR_NO_TEXTURE;
		}

		void add_materials()
		{
			for (unsigned int i = 0; i < scene.mNumMaterials; i++)
			{
				bool alpha_candidate = false;
				out.materials.push_back(convert_material(*scene.mMaterials[i], alpha_candidate));
				out.material_alpha_candidate.push_back(alpha_candidate);
			}
			if (out.materials.empty())
			{
				gpu::Material m = default_material();
				m.base_colour = { 0.8f, 0.8f, 0.8f, 1.0f };
				out.materials.push_back(m);
				out.material_alpha_candidate.push_back(false);
			}
			sg::u32 base = 0, normal = 0, metal_rough = 0, emissive = 0;
			for (const gpu::Material& m : out.materials)
			{
				base += m.base_colour_texture != PBR_NO_TEXTURE;
				normal += m.normal_texture != PBR_NO_TEXTURE;
				metal_rough += m.metal_rough_texture != PBR_NO_TEXTURE;
				emissive += m.emissive_texture != PBR_NO_TEXTURE || luminance(m.emissive) > 0.0f;
			}
			seWriteLine("PBR: %zu materials: %u base colour, %u normal, %u metal / roughness textures, %u emissive", out.materials.size(), base, normal, metal_rough, emissive);
		}

		gpu::Material convert_material(const aiMaterial& m, bool& alpha_candidate)
		{
			gpu::Material d = default_material();
			d.base_colour_texture = first_texture(m, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, true);
			aiColor4D colour;
			if (gltf)
			{
				// glTF factors are linear
				if (m.Get(AI_MATKEY_BASE_COLOR, colour) == aiReturn_SUCCESS)
				{
					d.base_colour = { colour.r, colour.g, colour.b, colour.a };
				}
				m.Get(AI_MATKEY_METALLIC_FACTOR, d.metallic);
				m.Get(AI_MATKEY_ROUGHNESS_FACTOR, d.roughness);
				// aiTextureType_UNKNOWN: AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE of older importers
				d.metal_rough_texture = first_texture(m, { aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN }, false);
				d.occlusion_texture = first_texture(m, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP }, false);
				aiString alpha_mode;
				if (m.Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode) == aiReturn_SUCCESS && (strcmp(alpha_mode.C_Str(), "MASK") == 0 || strcmp(alpha_mode.C_Str(), "BLEND") == 0))
				{
					d.flags |= PBR_MATERIAL_ALPHA_MASK;
					m.Get(AI_MATKEY_GLTF_ALPHACUTOFF, d.alpha_cutoff);
				}
				float strength = 1.0f;
				m.Get(AI_MATKEY_EMISSIVE_INTENSITY, strength);
				if (m.Get(AI_MATKEY_COLOR_EMISSIVE, colour) == aiReturn_SUCCESS)
				{
					d.emissive = { colour.r * strength, colour.g * strength, colour.b * strength };
				}
			}
			else
			{
				// Legacy (OBJ, FBX) colours are display (sRGB) values. A diffuse texture replaces the colour: their diffuse
				// factors are mostly left over Phong tints (Sponza's 0.588 grey would only darken the textures).
				if (d.base_colour_texture == PBR_NO_TEXTURE && (m.Get(AI_MATKEY_BASE_COLOR, colour) == aiReturn_SUCCESS || m.Get(AI_MATKEY_COLOR_DIFFUSE, colour) == aiReturn_SUCCESS))
				{
					const XMFLOAT3 c = linear_colour(colour, true);
					d.base_colour = { c.x, c.y, c.z, 1.0f };
				}
				// There is no transparency: mostly transparent surfaces (clear glass, water, ice) are cut out entirely, denser
				// ones (wine, beer) render opaque
				float opacity = 1.0f;
				if (m.Get(AI_MATKEY_OPACITY, opacity) == aiReturn_SUCCESS && opacity < 0.5f)
				{
					d.base_colour.w = 0.0f;
					d.flags |= PBR_MATERIAL_ALPHA_MASK;
				}
				d.metal_rough_texture = texture(m, aiTextureType_SPECULAR, false);
				if (specular_is_orm)
				{
					// ORCA scenes (Bistro, Sun Temple, Emerald Square): "specular" holds occlusion, roughness and metallic
					const bool textured = d.metal_rough_texture != PBR_NO_TEXTURE;
					d.metallic = textured ? 1.0f : 0.0f;
					d.roughness = textured ? 1.0f : 0.6f;
				}
				else
				{
					float shininess = 0.0f;
					if (m.Get(AI_MATKEY_SHININESS, shininess) == aiReturn_SUCCESS && shininess > 0.0f)
					{
						// Blinn-Phong exponent to GGX roughness (alpha = sqrt(2 / (n + 2)), roughness = sqrt(alpha))
						d.roughness = std::clamp(std::sqrt(std::sqrt(2.0f / (shininess + 2.0f))), 0.2f, 1.0f);
					}
					d.flags |= d.metal_rough_texture != PBR_NO_TEXTURE ? PBR_MATERIAL_LEGACY_SPECULAR : 0u;
				}
				d.opacity_texture = texture(m, aiTextureType_OPACITY, false);
				if (d.opacity_texture != PBR_NO_TEXTURE)
				{
					d.flags |= PBR_MATERIAL_ALPHA_MASK;
				}
				else if (d.base_colour_texture != PBR_NO_TEXTURE && out.textures[d.base_colour_texture].may_have_alpha)
				{
					alpha_candidate = true; // Decided by the GPU alpha analysis once the texture is uploaded
				}
				if (m.Get(AI_MATKEY_COLOR_EMISSIVE, colour) == aiReturn_SUCCESS)
				{
					d.emissive = linear_colour(colour, true);
				}
			}

			d.normal_texture = texture(m, aiTextureType_NORMALS, false);
			aiString bump;
			if (d.normal_texture == PBR_NO_TEXTURE && m.GetTexture(aiTextureType_HEIGHT, 0, &bump) == aiReturn_SUCCESS)
			{
				// OBJ files store normal maps as "bump" maps (Sponza's *_ddn); real height maps are not supported
				const std::string name = lower(bump.C_Str());
				if (name.find("ddn") != std::string::npos || name.find("normal") != std::string::npos || name.find("nrm") != std::string::npos)
				{
					d.normal_texture = texture(m, aiTextureType_HEIGHT, false);
				}
			}
			d.emissive_texture = texture(m, aiTextureType_EMISSIVE, true);
			if (d.emissive_texture != PBR_NO_TEXTURE && luminance(d.emissive) == 0.0f)
			{
				d.emissive = { 1.0f, 1.0f, 1.0f };
			}
			int two_sided = 0;
			if ((m.Get(AI_MATKEY_TWOSIDED, two_sided) == aiReturn_SUCCESS && two_sided != 0) || (d.flags & PBR_MATERIAL_ALPHA_MASK))
			{
				d.flags |= PBR_MATERIAL_DOUBLE_SIDED; // Cut outs are leaves, cards and fabric seen from both sides
			}
			return d;
		}

		void add_meshes()
		{
			mesh_lookup.assign(scene.mNumMeshes, ~0u);
			for (unsigned int i = 0; i < scene.mNumMeshes; i++)
			{
				add_mesh(*scene.mMeshes[i], i);
			}
		}

		void add_mesh(const aiMesh& am, unsigned int assimp_index)
		{
			std::vector<sg::u32> indices;
			indices.reserve(static_cast<size_t>(am.mNumFaces) * 3);
			for (unsigned int f = 0; f < am.mNumFaces; f++)
			{
				if (am.mFaces[f].mNumIndices == 3)
				{
					indices.insert(indices.end(), am.mFaces[f].mIndices, am.mFaces[f].mIndices + 3);
				}
			}
			if (!(am.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) || am.mNumVertices == 0 || indices.empty())
			{
				return;
			}
			gpu::Mesh mesh = {};
			mesh.vertex_offset = static_cast<sg::u32>(out.vertices.size());
			mesh.vertex_count = am.mNumVertices;
			mesh.index_offset = static_cast<sg::u32>(out.indices.size());
			mesh.material = std::min<sg::u32>(am.mMaterialIndex, static_cast<sg::u32>(out.materials.size()) - 1);

			Bounds mesh_bounds;
			for (unsigned int v = 0; v < am.mNumVertices; v++)
			{
				gpu::Vertex vertex = {};
				vertex.position = { am.mVertices[v].x, am.mVertices[v].y, am.mVertices[v].z };
				vertex.normal = am.mNormals ? XMFLOAT3(am.mNormals[v].x, am.mNormals[v].y, am.mNormals[v].z) : XMFLOAT3(0.0f, 1.0f, 0.0f);
				if (am.mTangents && am.mBitangents)
				{
					const XMVECTOR n = XMLoadFloat3(&vertex.normal);
					const XMVECTOR t = XMVectorSet(am.mTangents[v].x, am.mTangents[v].y, am.mTangents[v].z, 0.0f);
					const XMVECTOR b = XMVectorSet(am.mBitangents[v].x, am.mBitangents[v].y, am.mBitangents[v].z, 0.0f);
					const float sign = XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), b)) < 0.0f ? -1.0f : 1.0f;
					vertex.tangent = { am.mTangents[v].x, am.mTangents[v].y, am.mTangents[v].z, sign };
				}
				vertex.uv = am.mTextureCoords[0] ? XMFLOAT2(am.mTextureCoords[0][v].x, am.mTextureCoords[0][v].y) : XMFLOAT2(0.0f, 0.0f);
				mesh_bounds.extend(vertex.position);
				out.vertices.push_back(vertex);
			}
			mesh.aabb_min = { mesh_bounds.min.x, mesh_bounds.min.y, mesh_bounds.min.z, 0.0f };
			mesh.aabb_max = { mesh_bounds.max.x, mesh_bounds.max.y, mesh_bounds.max.z, 0.0f };

			meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), am.mNumVertices);
			mesh.index_count = static_cast<sg::u32>(indices.size());
			const gpu::Vertex* vertices = &out.vertices[mesh.vertex_offset];
			// Average texel density: alpha tested ray hits pick a texture level from it without fetching positions
			double uv_area = 0.0, area = 0.0;
			for (size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				const gpu::Vertex& a = vertices[indices[i]];
				const gpu::Vertex& b = vertices[indices[i + 1]];
				const gpu::Vertex& c = vertices[indices[i + 2]];
				const XMVECTOR pa = XMLoadFloat3(&a.position);
				area += XMVectorGetX(XMVector3Length(XMVector3Cross(XMVectorSubtract(XMLoadFloat3(&b.position), pa), XMVectorSubtract(XMLoadFloat3(&c.position), pa))));
				uv_area += std::abs((b.uv.x - a.uv.x) * (c.uv.y - a.uv.y) - (b.uv.y - a.uv.y) * (c.uv.x - a.uv.x));
			}
			mesh.uv_density = area > 0.0 ? static_cast<float>(uv_area / area) : 1.0f;
			add_meshlets(mesh, indices, &vertices->position.x);
			out.indices.insert(out.indices.end(), indices.begin(), indices.end());
			mesh_lookup[assimp_index] = static_cast<sg::u32>(out.meshes.size());
			out.meshes.push_back(mesh);
		}

		// Meshlets of a mesh (vertex indices made absolute, so mesh shaders read the vertex buffer directly)
		void add_meshlets(gpu::Mesh& mesh, const std::vector<sg::u32>& indices, const float* positions)
		{
			const size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), PBR_MESHLET_MAX_VERTICES, PBR_MESHLET_MAX_TRIANGLES);
			std::vector<meshopt_Meshlet> meshlets(max_meshlets);
			std::vector<unsigned int> meshlet_vertices(max_meshlets * PBR_MESHLET_MAX_VERTICES);
			std::vector<unsigned char> meshlet_triangles(max_meshlets * PBR_MESHLET_MAX_TRIANGLES * 3);
			const size_t meshlet_count = meshopt_buildMeshlets(meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(), indices.data(), indices.size(),
				positions, mesh.vertex_count, sizeof(gpu::Vertex), PBR_MESHLET_MAX_VERTICES, PBR_MESHLET_MAX_TRIANGLES, 0.25f);
			mesh.meshlet_offset = static_cast<sg::u32>(out.meshlets.size());
			mesh.meshlet_count = static_cast<sg::u32>(meshlet_count);
			for (size_t i = 0; i < meshlet_count; i++)
			{
				const meshopt_Meshlet& ml = meshlets[i];
				unsigned int* local_vertices = &meshlet_vertices[ml.vertex_offset];
				unsigned char* local_triangles = &meshlet_triangles[ml.triangle_offset];
				meshopt_optimizeMeshlet(local_vertices, local_triangles, ml.triangle_count, ml.vertex_count);
				const meshopt_Bounds b = meshopt_computeMeshletBounds(local_vertices, local_triangles, ml.triangle_count, positions, mesh.vertex_count, sizeof(gpu::Vertex));
				gpu::Meshlet g = {};
				g.vertex_offset = static_cast<sg::u32>(out.meshlet_vertices.size());
				g.triangle_offset = static_cast<sg::u32>(out.meshlet_triangles.size());
				g.vertex_count = ml.vertex_count;
				g.triangle_count = ml.triangle_count;
				g.sphere = { b.center[0], b.center[1], b.center[2], b.radius };
				g.cone_apex = { b.cone_apex[0], b.cone_apex[1], b.cone_apex[2], b.cone_cutoff };
				g.cone_axis = { b.cone_axis[0], b.cone_axis[1], b.cone_axis[2], 0.0f };
				for (unsigned int v = 0; v < ml.vertex_count; v++)
				{
					out.meshlet_vertices.push_back(mesh.vertex_offset + local_vertices[v]);
				}
				for (unsigned int t = 0; t < ml.triangle_count; t++)
				{
					const unsigned char* tri = &local_triangles[t * 3];
					out.meshlet_triangles.push_back(sg::u32(tri[0]) | (sg::u32(tri[1]) << 8) | (sg::u32(tri[2]) << 16));
				}
				out.meshlets.push_back(g);
			}
		}

		// One instance per mesh of every node, with the node's world transform (scaled to metres)
		void add_instances()
		{
			struct NodeEntry { const aiNode* node; XMMATRIX parent; };
			std::vector<NodeEntry> stack = { { scene.mRootNode, XMMatrixScaling(scale, scale, scale) } };
			while (!stack.empty())
			{
				const NodeEntry entry = stack.back();
				stack.pop_back();
				const XMMATRIX world = XMMatrixMultiply(to_xm(entry.node->mTransformation), entry.parent);
				for (unsigned int i = 0; i < entry.node->mNumMeshes; i++)
				{
					if (const sg::u32 mesh_index = mesh_lookup[entry.node->mMeshes[i]]; mesh_index != ~0u)
					{
						add_instance(mesh_index, world);
					}
				}
				for (unsigned int i = 0; i < entry.node->mNumChildren; i++)
				{
					stack.push_back({ entry.node->mChildren[i], world });
				}
			}
		}

		void add_instance(sg::u32 mesh_index, FXMMATRIX world)
		{
			const gpu::Mesh& mesh = out.meshes[mesh_index];
			gpu::Instance instance = {};
			XMStoreFloat4x4(&instance.world, world);
			XMStoreFloat4x4(&instance.previous_world, world);
			XMMATRIX normal_matrix = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
			normal_matrix.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
			XMStoreFloat4x4(&instance.normal_matrix, normal_matrix);
			instance.mesh = mesh_index;
			instance.material = mesh.material;
			instance.flags = XMVectorGetX(XMMatrixDeterminant(world)) < 0.0f ? PBR_INSTANCE_MIRRORED : 0u;
			Bounds local;
			local.extend({ mesh.aabb_min.x, mesh.aabb_min.y, mesh.aabb_min.z });
			local.extend({ mesh.aabb_max.x, mesh.aabb_max.y, mesh.aabb_max.z });
			const Bounds world_bounds = local.transformed(world);
			const XMFLOAT3 centre = world_bounds.centre();
			instance.world_sphere = { centre.x, centre.y, centre.z, world_bounds.radius() };
			out.bounds.extend(world_bounds);
			out.instances.push_back(instance);
		}

		// Emissive triangles for light sampling, weighted by emitted power (material emission luminance * world area; the
		// texture modulates the sample but not the weight, so shaders can recompute a hit's sampling probability)
		void add_emissive_triangles()
		{
			std::vector<double> weights;
			for (sg::u32 instance_index = 0; instance_index < out.instances.size(); instance_index++)
			{
				const gpu::Instance& instance = out.instances[instance_index];
				const double emission = luminance(out.materials[instance.material].emissive);
				if (emission <= 0.0)
				{
					continue;
				}
				const gpu::Mesh& mesh = out.meshes[instance.mesh];
				const XMMATRIX world = XMLoadFloat4x4(&instance.world);
				for (sg::u32 t = 0; t < mesh.index_count / 3; t++)
				{
					XMVECTOR p[3];
					for (int k = 0; k < 3; k++)
					{
						p[k] = XMVector3TransformCoord(XMLoadFloat3(&out.vertices[mesh.vertex_offset + out.indices[mesh.index_offset + t * 3 + k]].position), world);
					}
					const double area = 0.5 * XMVectorGetX(XMVector3Length(XMVector3Cross(XMVectorSubtract(p[1], p[0]), XMVectorSubtract(p[2], p[0]))));
					if (area > 0.0)
					{
						out.emissive_triangles.push_back({ instance_index, t, 1.0f, 0 });
						weights.push_back(emission * area);
						out.emissive_weight += emission * area;
					}
				}
			}
			if (out.emissive_weight > 0.0)
			{
				build_alias_table(weights, out.emissive_weight, out.emissive_triangles);
			}
		}

		// The file's first camera, in world space
		void read_camera()
		{
			if (scene.mNumCameras == 0)
			{
				return;
			}
			const aiCamera& camera = *scene.mCameras[0];
			const aiNode* camera_node = scene.mRootNode->FindNode(camera.mName);
			if (camera_node == nullptr)
			{
				return;
			}
			XMMATRIX world = XMMatrixIdentity();
			for (const aiNode* n = camera_node; n != nullptr; n = n->mParent)
			{
				world = XMMatrixMultiply(world, to_xm(n->mTransformation));
			}
			world = XMMatrixMultiply(world, XMMatrixScaling(scale, scale, scale));
			const XMVECTOR position = XMVector3TransformCoord(XMVectorSet(camera.mPosition.x, camera.mPosition.y, camera.mPosition.z, 1.0f), world);
			// aiProcess_ConvertToLeftHanded mirrors the node transforms but not the camera's local look direction, which then
			// points backwards (checked against the views the Bistro and Sun Temple files were authored with)
			const XMVECTOR forward = XMVectorNegate(XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(camera.mLookAt.x, camera.mLookAt.y, camera.mLookAt.z, 0.0f), world)));
			XMStoreFloat3(&out.camera_position, position);
			XMStoreFloat3(&out.camera_target, XMVectorAdd(position, forward));
			out.has_camera = true;
		}
	};

	std::shared_ptr<Scene::ImportedScene> Scene::import(std::string path, ScenePreset preset, bool has_preset)
	{
		const auto start = std::chrono::high_resolution_clock::now();
		std::shared_ptr<ImportedScene> out = std::make_shared<ImportedScene>();
		Assimp::Importer assimp;
		// Lines and points are dropped (meshlets and acceleration structures are built from triangles)
		assimp.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
		const aiScene* scene = assimp.ReadFile(path, aiProcess_Triangulate | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
			aiProcess_SortByPType | aiProcess_ConvertToLeftHanded | aiProcess_GenUVCoords | aiProcess_TransformUVCoords);
		if (scene == nullptr || scene->mRootNode == nullptr)
		{
			out->error = std::string("assimp: ") + assimp.GetErrorString();
			return out;
		}
		const std::string extension = lower(std::filesystem::path(path).extension().string());
		Importer importer{ *scene, path, has_preset ? preset.scale : 1.0f, has_preset && preset.specular_is_orm, extension == ".gltf" || extension == ".glb", *out };
		importer.add_materials();
		importer.add_meshes();
		importer.add_instances();
		importer.add_emissive_triangles();
		importer.read_camera();
		if (out->instances.empty())
		{
			out->error = "the scene has no triangle meshes";
		}
		out->seconds = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - start).count();
		return out;
	}

	// ---------------------------------------------------------------------------------------------------------------------

	Scene::Scene(sg::SharedPtr<sg::Device> device_) : device(device_)
	{
	}

	Scene::~Scene()
	{
		if (import_future.valid())
		{
			import_future.wait();
		}
		release();
	}

	void Scene::release()
	{
		for (const GPUTexture& t : textures)
		{
			if (t.bindless != ~0u)
			{
				device->free_bindless_descriptor(t.bindless);
			}
		}
		textures.clear();
		next_texture = 0;
		imported = nullptr;
		error.clear();
		statistics = {};
		bounds = {};
		has_file_camera = false;
		views = {};
		meshlet_visibility_bits = 0;
		max_tasks = 0;
		emissive_weight = 0.0f;
		pending_uploads.clear();
		buffers.clear();
		vertex_buffer = index_buffer = nullptr;
		materials.clear();
		instances.clear();
		alpha_counts = alpha_readback = nullptr;
		alpha_counts_uav = {};
		alpha_candidates.clear();
		blas.clear();
		blas_pools.clear();
		blas_pool_used = blas_pool_capacity = 0;
		tlas = tlas_instances = scratch = nullptr;
		next_blas = 0;
		state = State::Empty;
	}

	void Scene::load(const std::string& new_path, const ScenePreset* new_preset)
	{
		if (import_future.valid())
		{
			import_future.wait();
			import_future = {};
		}
		release();
		path = new_path;
		preset = new_preset;
		state = State::Importing;
		seWriteLine("PBR: importing %s", path.c_str());
		import_future = std::async(std::launch::async, &Scene::import, path, new_preset ? *new_preset : ScenePreset{}, new_preset != nullptr);
	}

	bool Scene::get_start_camera(XMFLOAT3& position, XMFLOAT3& target) const
	{
		const bool from_file = preset == nullptr || XMVector3Equal(XMLoadFloat3(&preset->camera_position), XMLoadFloat3(&preset->camera_target));
		position = from_file ? file_camera_position : preset->camera_position;
		target = from_file ? file_camera_target : preset->camera_target;
		return !from_file || has_file_camera;
	}

	std::string Scene::get_status() const
	{
		char text[256];
		switch (state)
		{
		case State::Empty: return "No scene";
		case State::Importing: return "Importing " + path;
		case State::Uploading:
			sprintf_s(text, "Uploading: %zu / %zu textures, %zu buffers left", next_texture, imported ? imported->textures.size() : 0, pending_uploads.size());
			return text;
		case State::AnalysingAlpha: return "Finding alpha tested materials";
		case State::BuildingAccelerationStructures:
			sprintf_s(text, "Building acceleration structures: %zu / %zu", next_blas, blas.size());
			return text;
		case State::Ready: return "Ready";
		case State::Failed: return "Failed: " + error;
		}
		return {};
	}

	template <typename T>
	sg::SharedPtr<sg::Buffer> Scene::create_buffer(const std::vector<T>& data, sg::ShaderResourceView& srv)
	{
		sg::SharedPtr<sg::Buffer> buffer = create_padded_buffer(*device, data.size() * sizeof(T), sg::BufferType::GeneralDataBuffer, false);
		srv = device->create_shader_resource_view(buffer, sizeof(T), std::max<size_t>(data.size(), 1));
		if (!data.empty())
		{
			const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
			pending_uploads.push_back({ buffer, std::make_shared<std::vector<uint8_t>>(bytes, bytes + data.size() * sizeof(T)), 0 });
		}
		buffers.push_back(buffer);
		return buffer;
	}

	bool Scene::upload_pending_buffers(sg::Ptr<sg::UploadHeap>& upload_heap)
	{
		for (sg::u64 budget = kUploadBudget; !pending_uploads.empty() && budget > 0;)
		{
			PendingUpload& upload = pending_uploads.front();
			const sg::u32 chunk = static_cast<sg::u32>(std::min<size_t>(upload.data->size() - upload.offset, budget));
			const sg::UploadHeap::Offset offset = upload_heap->allocate_upload_memory(chunk, 256);
			if (offset == sg::UploadHeap::INVALID_OFFSET)
			{
				return false; // The heap is full this frame
			}
			upload_heap->write_upload_memory(offset, upload.data->data() + upload.offset, chunk);
			upload_heap->upload_to_buffer(upload.buffer.get(), static_cast<sg::u32>(upload.offset), offset, chunk);
			upload.offset += chunk;
			budget -= chunk;
			if (upload.offset == upload.data->size())
			{
				pending_uploads.erase(pending_uploads.begin());
			}
		}
		return pending_uploads.empty();
	}

	void Scene::begin_upload()
	{
		ImportedScene& s = *imported;
		statistics.import_seconds = s.seconds;
		statistics.meshes = static_cast<sg::u32>(s.meshes.size());
		statistics.vertices = static_cast<sg::u32>(s.vertices.size());
		statistics.materials = static_cast<sg::u32>(s.materials.size());
		statistics.textures = static_cast<sg::u32>(s.textures.size());
		statistics.instances = static_cast<sg::u32>(s.instances.size());
		statistics.emissive_triangles = static_cast<sg::u32>(s.emissive_triangles.size());
		bounds = s.bounds;
		emissive_weight = static_cast<float>(s.emissive_weight);
		has_file_camera = s.has_camera;
		file_camera_position = s.camera_position;
		file_camera_target = s.camera_target;
		if (has_file_camera)
		{
			seWriteLine("PBR: file camera at (%.2f %.2f %.2f) looking at (%.2f %.2f %.2f)", file_camera_position.x, file_camera_position.y, file_camera_position.z,
				file_camera_target.x, file_camera_target.y, file_camera_target.z);
		}

		// Meshlet visibility bits and amplification task counts per instance
		for (gpu::Instance& instance : s.instances)
		{
			const gpu::Mesh& mesh = s.meshes[instance.mesh];
			instance.meshlet_visibility_offset = meshlet_visibility_bits;
			meshlet_visibility_bits += mesh.meshlet_count;
			max_tasks += (mesh.meshlet_count + PBR_TASK_MESHLETS - 1) / PBR_TASK_MESHLETS;
			statistics.meshlets += mesh.meshlet_count;
			statistics.triangles += mesh.index_count / 3;
		}

		vertex_buffer = create_buffer(s.vertices, views.vertices);
		index_buffer = create_buffer(s.indices, views.indices);
		create_buffer(s.meshlets, views.meshlets);
		create_buffer(s.meshlet_vertices, views.meshlet_vertices);
		create_buffer(s.meshlet_triangles, views.meshlet_triangles);
		create_buffer(s.meshes, views.meshes);
		create_buffer(s.emissive_triangles, views.emissive_triangles);
		materials = s.materials;
		instances = s.instances;
		// Freed as soon as uploaded (the pending uploads hold copies)
		std::vector<gpu::Vertex>().swap(s.vertices);
		std::vector<sg::u32>().swap(s.meshlet_vertices);
		std::vector<sg::u32>().swap(s.meshlet_triangles);
		textures.resize(s.textures.size());
		seWriteLine("PBR: imported %s in %.1f s: %u instances, %u meshes, %u meshlets, %u triangles, %u materials, %u textures, bounds (%.1f %.1f %.1f) - (%.1f %.1f %.1f)",
			path.c_str(), s.seconds, statistics.instances, statistics.meshes, statistics.meshlets, statistics.triangles, statistics.materials, statistics.textures,
			bounds.min.x, bounds.min.y, bounds.min.z, bounds.max.x, bounds.max.y, bounds.max.z);
	}

	bool Scene::upload_textures(sg::Ptr<sg::UploadHeap>& upload_heap)
	{
		for (sg::u64 budget = kUploadBudget; next_texture < imported->textures.size() && budget > 0; next_texture++)
		{
			Texture& source = imported->textures[next_texture];
			const sg::u64 bytes = source.data.size();
			if (bytes > budget && budget != kUploadBudget)
			{
				return false; // Next frame (a texture larger than the budget is uploaded alone)
			}
			GPUTexture& gpu_texture = textures[next_texture];
			if (source.dds)
			{
				gpu_texture.texture = device->create_texture_from_dds(source.data.data(), source.data.size(), *upload_heap);
			}
			else
			{
				sg::ResourceCreateDesc desc(source.width, source.height, source.srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM);
				desc.mip_count = source.mip_count;
				gpu_texture.texture = sg::create_texture(*device, desc);
				const uint8_t* mip_data = source.data.data();
				for (sg::u32 mip = 0, w = source.width, h = source.height; mip < source.mip_count; mip++, w = std::max(1u, w / 2), h = std::max(1u, h / 2))
				{
					upload_heap->upload_texture_subresource(gpu_texture.texture.get(), mip, mip_data, w * 4, h);
					mip_data += static_cast<size_t>(w) * h * 4;
				}
			}
			if (gpu_texture.texture)
			{
				sg::ShaderResourceView srv = device->create_shader_resource_view(gpu_texture.texture);
				gpu_texture.bindless = device->allocate_bindless_descriptor(srv);
				statistics.texture_bytes += bytes;
			}
			else
			{
				seWriteLine("PBR: texture %s failed to load", source.key.c_str());
			}
			std::vector<uint8_t>().swap(source.data);
			budget = bytes >= budget ? 0 : budget - bytes;
		}
		return next_texture == imported->textures.size();
	}

	void Scene::analyse_alpha(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer)
	{
		// Material textures become bindless indices; materials whose textures failed to load lose them. Colour textures
		// loaded from DDS files in UNORM formats hold sRGB values: shaders decode them (RGBA8 textures decoded from other files
		// are created with _SRGB formats).
		auto loaded = [&](sg::u32 texture) { return texture < textures.size() && textures[texture].bindless != ~0u; };
		auto srgb_values_in_unorm = [&](sg::u32 texture) { return loaded(texture) && imported->textures[texture].dds && !is_srgb_format(textures[texture].texture->resource_create_desc.format); };
		for (size_t i = 0; i < materials.size(); i++)
		{
			gpu::Material& m = materials[i];
			m.flags |= srgb_values_in_unorm(m.base_colour_texture) ? PBR_MATERIAL_BASE_COLOUR_SRGB : 0u;
			m.flags |= srgb_values_in_unorm(m.emissive_texture) ? PBR_MATERIAL_EMISSIVE_SRGB : 0u;
			for (sg::u32* texture : { &m.base_colour_texture, &m.normal_texture, &m.metal_rough_texture, &m.emissive_texture, &m.occlusion_texture, &m.opacity_texture })
			{
				*texture = loaded(*texture) ? textures[*texture].bindless : PBR_NO_TEXTURE;
			}
			if (imported->material_alpha_candidate[i] && m.base_colour_texture != PBR_NO_TEXTURE)
			{
				alpha_candidates.push_back(static_cast<sg::u32>(i));
			}
		}
		if (alpha_candidates.empty())
		{
			return;
		}

		// Counts texels below the alpha cutoff on a 64 x 64 grid of each candidate's base colour texture
		sg::BindingDesc binding_desc;
		binding_desc.cbv_binding_count = 1;
		binding_desc.uav_binding_count = 1;
		binding_desc.sampler_binding_count = 6;
		binding_desc.bindless = true;
		if (!alpha_analysis_pipeline)
		{
			sg::PipelineDesc::Compute desc;
			desc.compute_shader = sg::create_compute_shader(*device, get_shader_path("PBR_AlphaAnalysis"));
			alpha_analysis_pipeline = device->create_pipeline(desc, binding_desc);
		}
		alpha_counts = create_padded_buffer(*device, alpha_candidates.size() * 4, sg::BufferType::GeneralDataBuffer, true);
		alpha_counts_uav = device->create_unordered_access_view(alpha_counts, 4, alpha_candidates.size());
		alpha_readback = sg::create_readback_buffer(*device, static_cast<sg::u32>(sg::AlignUp<size_t>(alpha_candidates.size() * 4, 256)));
		sg::ShaderResourceView no_srv = {};
		command_list.clear_buffer_uint(alpha_counts_uav, no_srv, 0);

		command_list.set_pipeline(alpha_analysis_pipeline.get());
		for (size_t i = 0; i < alpha_candidates.size(); i++)
		{
			const gpu::Material& m = materials[alpha_candidates[i]];
			struct { sg::u32 texture, output; float cutoff; sg::u32 pad; } constants = { m.base_colour_texture, static_cast<sg::u32>(i), m.alpha_cutoff, 0 };
			sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(constants);
			sg::Binding binding;
			binding.cbv_binding_count = binding_desc.cbv_binding_count;
			binding.uav_binding_count = binding_desc.uav_binding_count;
			binding.sampler_binding_count = binding_desc.sampler_binding_count;
			binding.set_cbv(cbv, 0);
			binding.set_uav(alpha_counts_uav, 0);
			command_list.bind(binding, sg::PipelineType::Compute);
			command_list.dispatch(64 / 8, 64 / 8, 1);
		}
		sg::Binding unbind;
		command_list.bind(unbind, sg::PipelineType::Compute);
		command_list.copy_buffer_to_buffer(alpha_readback.get(), alpha_counts.get());
	}

	void Scene::finish_alpha_analysis()
	{
		std::vector<sg::u32> counts(alpha_candidates.size());
		if (!counts.empty())
		{
			alpha_readback->read_memory(0, counts.data(), counts.size() * 4);
		}
		for (size_t i = 0; i < alpha_candidates.size(); i++)
		{
			// Some cut out texels make it alpha tested. Textures whose alpha is (nearly) all below the cutoff have an unused
			// alpha channel instead: testing would make the surface invisible.
			const float fraction = counts[i] / 4096.0f;
			if (fraction > 0.002f && fraction < 0.98f)
			{
				materials[alpha_candidates[i]].flags |= PBR_MATERIAL_ALPHA_MASK | PBR_MATERIAL_DOUBLE_SIDED;
				seWriteLine("PBR: material %u is alpha tested (%.1f%% of its texture cut out)", alpha_candidates[i], fraction * 100.0f);
			}
		}
		for (const gpu::Material& m : materials)
		{
			statistics.alpha_tested_materials += (m.flags & PBR_MATERIAL_ALPHA_MASK) ? 1u : 0u;
		}
		sg::u64 alpha_triangles = 0;
		for (gpu::Instance& instance : instances)
		{
			const sg::u32 flags = materials[instance.material].flags;
			instance.flags |= (flags & (PBR_MATERIAL_ALPHA_MASK | PBR_MATERIAL_DOUBLE_SIDED)) ? PBR_INSTANCE_BUCKET_MASK : 0u; // PBR_BUCKET_MASKED
			alpha_triangles += (flags & PBR_MATERIAL_ALPHA_MASK) ? imported->meshes[instance.mesh].index_count / 3 : 0;
		}
		seWriteLine("PBR: %u alpha tested materials, %llu alpha tested triangles", statistics.alpha_tested_materials, alpha_triangles);
		alpha_counts = nullptr;
		alpha_readback = nullptr;
		alpha_counts_uav = {};
		create_buffer(materials, views.materials);
		create_buffer(instances, views.instances);
	}

	void Scene::build_acceleration_structures(sg::CommandList& command_list, sg::Ptr<sg::UploadHeap>& upload_heap)
	{
		const ImportedScene& s = *imported;
		auto geometry_of = [&](const gpu::Mesh& mesh)
			{
				sg::Raytracing::GeometryDesc geometry;
				geometry.vertex_buffer = vertex_buffer.get();
				geometry.vertex_offset = static_cast<sg::u64>(mesh.vertex_offset) * sizeof(gpu::Vertex);
				geometry.vertex_stride = sizeof(gpu::Vertex);
				geometry.vertex_count = mesh.vertex_count;
				geometry.index_buffer = index_buffer.get();
				geometry.index_offset = static_cast<sg::u64>(mesh.index_offset) * sizeof(sg::u32);
				geometry.index_count = mesh.index_count;
				// Alpha tested geometry reports candidate hits, which ray queries test against the opacity texture
				geometry.opaque = (materials[mesh.material].flags & PBR_MATERIAL_ALPHA_MASK) == 0;
				return geometry;
			};
		if (blas.empty())
		{
			// One scratch buffer, sized for the largest build (builds are serialised by the UAV barrier after each)
			blas.resize(s.meshes.size());
			sg::u64 scratch_size = device->get_top_level_prebuild_info(static_cast<sg::u32>(instances.size())).scratch_size;
			for (const gpu::Mesh& mesh : s.meshes)
			{
				const sg::Raytracing::GeometryDesc geometry = geometry_of(mesh);
				scratch_size = std::max(scratch_size, device->get_bottom_level_prebuild_info(&geometry, 1).scratch_size);
			}
			scratch = sg::create_buffer(*device, static_cast<sg::u32>(scratch_size), sg::BufferType::GeneralDataBuffer, true);
		}

		for (sg::u32 builds = 0; next_blas < s.meshes.size() && builds < kBlasBuildsPerFrame; next_blas++, builds++)
		{
			const sg::Raytracing::GeometryDesc geometry = geometry_of(s.meshes[next_blas]);
			const sg::u64 size = device->get_bottom_level_prebuild_info(&geometry, 1).result_size;
			// Packed into pool buffers (placed buffers align to 64 KiB, acceleration structures only need 256 bytes)
			if (blas_pools.empty() || blas_pool_used + size > blas_pool_capacity)
			{
				blas_pool_capacity = std::max(kBlasPoolBytes, size);
				blas_pools.push_back(sg::create_buffer(*device, static_cast<sg::u32>(blas_pool_capacity), sg::BufferType::AccelerationStructure, true));
				blas_pool_used = 0;
			}
			blas[next_blas] = { static_cast<sg::u32>(blas_pools.size() - 1), blas_pool_used };
			command_list.build_bottom_level_acceleration_structure(&geometry, 1, blas_pools.back().get(), scratch.get(), blas_pool_used);
			blas_pool_used += size;
			statistics.acceleration_structure_bytes += size;
		}
		if (next_blas < s.meshes.size())
		{
			return;
		}

		// Top level: every instance, InstanceID() = instance index
		std::vector<sg::Raytracing::InstanceDesc> descs(instances.size());
		for (size_t i = 0; i < instances.size(); i++)
		{
			const gpu::Instance& instance = instances[i];
			sg::Raytracing::InstanceDesc& d = descs[i];
			// Row vector world matrix -> 3x4 column vector transform (its transpose)
			for (int r = 0; r < 3; r++)
			{
				for (int c = 0; c < 4; c++)
				{
					d.transform[r][c] = instance.world.m[c][r];
				}
			}
			d.instance_id = static_cast<sg::u32>(i);
			d.instance_mask = 0xFF;
			d.contribution_to_hit_group = 0;
			d.flags = sg::Raytracing::InstanceFlagTriangleCullDisable;
			d.acceleration_structure = device->get_gpu_address(blas_pools[blas[instance.mesh].pool].get()) + blas[instance.mesh].offset;
		}
		const sg::u32 instance_bytes = static_cast<sg::u32>(descs.size() * sizeof(sg::Raytracing::InstanceDesc));
		tlas_instances = create_padded_buffer(*device, instance_bytes, sg::BufferType::GeneralDataBuffer, false);
		const sg::UploadHeap::Offset offset = upload_heap->allocate_upload_memory(instance_bytes, 256);
		seAssert(offset != sg::UploadHeap::INVALID_OFFSET, "Upload heap full");
		upload_heap->write_upload_memory(offset, descs.data(), instance_bytes);
		upload_heap->upload_to_buffer(tlas_instances.get(), 0, offset, instance_bytes);

		const sg::u64 tlas_size = device->get_top_level_prebuild_info(static_cast<sg::u32>(descs.size())).result_size;
		tlas = sg::create_buffer(*device, static_cast<sg::u32>(tlas_size), sg::BufferType::AccelerationStructure, true);
		command_list.build_top_level_acceleration_structure(tlas_instances.get(), static_cast<sg::u32>(descs.size()), tlas.get(), scratch.get());
		statistics.acceleration_structure_bytes += tlas_size;
		views.tlas = device->create_acceleration_structure_view(tlas);
		state = State::Ready;
		imported = nullptr; // CPU copies are no longer needed
		seWriteLine("PBR: scene ready (%u alpha tested materials, %.1f MiB textures, %.1f MiB acceleration structures)", statistics.alpha_tested_materials,
			statistics.texture_bytes / 1048576.0, statistics.acceleration_structure_bytes / 1048576.0);
	}

	void Scene::update(sg::CommandList& command_list, sg::Ptr<sg::UploadHeap>& upload_heap, SimpleLinearConstantBuffer& cbuffer)
	{
		switch (state)
		{
		case State::Importing:
			if (import_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				return;
			}
			imported = import_future.get();
			if (!imported->error.empty())
			{
				error = imported->error;
				seWriteLine("PBR: loading %s failed: %s", path.c_str(), error.c_str());
				imported = nullptr;
				state = State::Failed;
				return;
			}
			begin_upload();
			state = State::Uploading;
			[[fallthrough]];
		case State::Uploading:
			if (upload_pending_buffers(upload_heap) && upload_textures(upload_heap))
			{
				analyse_alpha(command_list, cbuffer);
				state = State::AnalysingAlpha;
			}
			return;
		case State::AnalysingAlpha:
			// The frame that recorded the analysis has finished (the application waits for every frame)
			finish_alpha_analysis();
			state = State::BuildingAccelerationStructures;
			return;
		case State::BuildingAccelerationStructures:
			// Material and instance buffers first (the BLAS builds read the vertex and index buffers, already uploaded)
			if (upload_pending_buffers(upload_heap))
			{
				build_acceleration_structures(command_list, upload_heap);
			}
			return;
		default:
			return;
		}
	}
}
