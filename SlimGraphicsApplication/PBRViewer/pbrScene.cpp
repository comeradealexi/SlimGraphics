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
			{ "Bistro Exterior", "../SlimGraphicsAssets/Bistro_v5_2/BistroExterior.fbx", 1.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 35.0f, 230.0f, true, "PBRViewer/CameraTracks/BistroExterior.json" },
			{ "Bistro Exterior (Dusk)", "../SlimGraphicsAssets/Bistro_v5_2/BistroExterior.fbx", 1.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, -4.0f, 230.0f, true, "PBRViewer/CameraTracks/BistroExterior.json" },
			{ "Bistro Interior", "../SlimGraphicsAssets/Bistro_v5_2/BistroInterior_Wine.fbx", 1.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 30.0f, 300.0f, true, "PBRViewer/CameraTracks/BistroInterior.json" },
			{ "Sun Temple", "../SlimGraphicsAssets/UE4SunTemple_v4/SunTemple/SunTemple.fbx", 1.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 60.0f, 20.0f, true, "PBRViewer/CameraTracks/SunTemple.json" },
			{ "Emerald Square", "../SlimGraphicsAssets/EmeraldSquare_v4_1/EmeraldSquare_Day.fbx", 1.0f, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 40.0f, 140.0f, true, "PBRViewer/CameraTracks/EmeraldSquare.json" },
			{ "Damaged Helmet", "../SlimGraphicsAssets/Khronos/DamagedHelmet/glTF/DamagedHelmet.gltf", 1.0f, { 0.0f, 0.2f, -3.0f }, { 0.0f, 0.0f, 0.0f }, 35.0f, 240.0f, false, nullptr },
			{ "Sci-Fi Helmet", "../SlimGraphicsAssets/Khronos/SciFiHelmet/SciFiHelmet.gltf", 1.0f, { 0.0f, 0.5f, -3.5f }, { 0.0f, 0.0f, 0.0f }, 35.0f, 240.0f, false, nullptr },
			{ "Metal Rough Spheres", "../SlimGraphicsAssets/Khronos/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf", 1.0f, { 0.0f, 0.0f, -14.0f }, { 0.0f, 0.0f, 0.0f }, 40.0f, 30.0f, false, nullptr },
			{ "PICA PICA Robot", "../SlimGraphicsAssets/Khronos/pica_pica_robot/scene.gltf", 1.0f, { 0.0f, 1.2f, -4.0f }, { 0.0f, 0.9f, 0.0f }, 40.0f, 240.0f, false, nullptr },
		};
		return presets;
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
		XMFLOAT3 bounds_min = { FLT_MAX, FLT_MAX, FLT_MAX };
		XMFLOAT3 bounds_max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		bool has_camera = false;
		XMFLOAT3 camera_position = { 0, 0, 0 };
		XMFLOAT3 camera_target = { 0, 0, 1 };
		std::string error;
		float seconds = 0.0f;
	};

	namespace
	{
		constexpr sg::u64 kUploadBudget = 64ull << 20;	// Bytes of scene data uploaded per frame (the upload heap is per frame)
		constexpr sg::u32 kBlasBuildsPerFrame = 256;

		float srgb_to_linear(float c)
		{
			return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}

		float linear_to_srgb(float c)
		{
			return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
		}

		std::string lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
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

		// Texture paths in model files are often relative to another folder or absolute; try the usual places
		std::string resolve_texture_path(const std::string& model_path, std::string texture_path)
		{
			std::replace(texture_path.begin(), texture_path.end(), '\\', '/');
			const std::filesystem::path model_dir = std::filesystem::path(model_path).parent_path();
			const std::filesystem::path file_name = std::filesystem::path(texture_path).filename();
			const std::filesystem::path candidates[] =
			{
				model_dir / texture_path,
				model_dir / file_name,
				model_dir / "Textures" / file_name,
				model_dir / "textures" / file_name,
			};
			for (const std::filesystem::path& candidate : candidates)
			{
				std::error_code ec;
				if (std::filesystem::is_regular_file(candidate, ec))
				{
					return candidate.string();
				}
				// The same texture converted to DDS (common for FBX scenes authored with PNG / TGA references)
				std::filesystem::path dds = candidate;
				dds.replace_extension(".dds");
				if (std::filesystem::is_regular_file(dds, ec))
				{
					return dds.string();
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
			const uint32_t pf_flags = *reinterpret_cast<const uint32_t*>(&file[80]);
			const uint32_t four_cc = *reinterpret_cast<const uint32_t*>(&file[84]);
			auto fcc = [](const char* s) { return (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24); };
			if (pf_flags & 0x4) // DDPF_FOURCC
			{
				if (four_cc == fcc("DX10") && file.size() >= 148)
				{
					const uint32_t dxgi = *reinterpret_cast<const uint32_t*>(&file[128]);
					switch (dxgi)
					{
					case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
					case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8G8_UNORM:
						return false;
					default:
						return true;
					}
				}
				return four_cc == fcc("DXT1") || four_cc == fcc("DXT2") || four_cc == fcc("DXT3") || four_cc == fcc("DXT4") || four_cc == fcc("DXT5");
			}
			return (pf_flags & 0x1) != 0; // DDPF_ALPHAPIXELS
		}

		// RGBA8 mip chain with box filtering; colour (sRGB) textures are averaged in linear space
		void build_mips(const uint8_t* pixels, sg::u32 width, sg::u32 height, bool srgb, std::vector<uint8_t>& out, sg::u32& mip_count)
		{
			out.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
			mip_count = 1;
			static float to_linear[256];
			static bool table = false;
			if (!table)
			{
				for (int i = 0; i < 256; i++) to_linear[i] = srgb_to_linear(i / 255.0f);
				table = true;
			}
			size_t src_offset = 0;
			sg::u32 w = width, h = height;
			while (w > 1 || h > 1)
			{
				const sg::u32 nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
				const size_t dst_offset = out.size();
				out.resize(dst_offset + static_cast<size_t>(nw) * nh * 4);
				for (sg::u32 y = 0; y < nh; y++)
				{
					for (sg::u32 x = 0; x < nw; x++)
					{
						float sum[4] = {};
						for (sg::u32 dy = 0; dy < 2; dy++)
						{
							for (sg::u32 dx = 0; dx < 2; dx++)
							{
								const sg::u32 sx = std::min(x * 2 + dx, w - 1), sy = std::min(y * 2 + dy, h - 1);
								const uint8_t* p = &out[src_offset + (static_cast<size_t>(sy) * w + sx) * 4];
								for (int c = 0; c < 3; c++) sum[c] += srgb ? to_linear[p[c]] : p[c] / 255.0f;
								sum[3] += p[3] / 255.0f;
							}
						}
						uint8_t* d = &out[dst_offset + (static_cast<size_t>(y) * nw + x) * 4];
						for (int c = 0; c < 4; c++)
						{
							float v = sum[c] * 0.25f;
							if (srgb && c < 3) v = linear_to_srgb(v);
							d[c] = static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
						}
					}
				}
				src_offset = dst_offset;
				w = nw;
				h = nh;
				mip_count++;
			}
		}

		XMMATRIX to_xm(const aiMatrix4x4& m)
		{
			// Assimp matrices are row major for column vectors; DirectXMath uses row vectors, so transpose
			return XMMatrixTranspose(XMMATRIX(&m.a1));
		}
	}

	std::shared_ptr<Scene::ImportedScene> Scene::import(std::string path, ScenePreset preset, bool has_preset)
	{
		const auto start = std::chrono::high_resolution_clock::now();
		std::shared_ptr<ImportedScene> out = std::make_shared<ImportedScene>();

		Assimp::Importer importer;
		// Lines and points are dropped (meshlets and acceleration structures are built from triangles)
		importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
		const unsigned int flags = aiProcess_Triangulate | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
			aiProcess_SortByPType | aiProcess_ConvertToLeftHanded | aiProcess_GenUVCoords | aiProcess_TransformUVCoords;
		const aiScene* scene = importer.ReadFile(path, flags);
		if (scene == nullptr || scene->mRootNode == nullptr)
		{
			out->error = std::string("assimp: ") + importer.GetErrorString();
			return out;
		}
		const float scale = has_preset ? preset.scale : 1.0f;
		const bool specular_is_orm = has_preset && preset.specular_is_orm;
		const std::string ext = lower(std::filesystem::path(path).extension().string());
		const bool is_gltf = ext == ".gltf" || ext == ".glb";

		// --- Textures (deduplicated by file and colour space)
		std::unordered_map<std::string, sg::u32> texture_lookup;
		auto texture_index = [&](const aiMaterial& material, aiTextureType type, bool srgb) -> sg::u32
			{
				aiString texture_path;
				if (material.GetTexture(type, 0, &texture_path) != aiReturn_SUCCESS || texture_path.length == 0)
				{
					return PBR_NO_TEXTURE;
				}
				const aiTexture* embedded = scene->GetEmbeddedTexture(texture_path.C_Str());
				const std::string file = embedded ? std::string() : resolve_texture_path(path, texture_path.C_Str());
				if (!embedded && file.empty())
				{
					seWriteLine("PBR: texture not found: %s", texture_path.C_Str());
					return PBR_NO_TEXTURE;
				}
				const std::string key = (embedded ? path + "#" + texture_path.C_Str() : file) + (srgb ? "|srgb" : "|linear");
				auto it = texture_lookup.find(key);
				if (it != texture_lookup.end())
				{
					return it->second;
				}

				Texture texture;
				texture.key = key;
				texture.srgb = srgb;
				std::vector<uint8_t> encoded;
				if (embedded && embedded->mHeight != 0)
				{
					// Raw BGRA8 texels
					std::vector<uint8_t> rgba(static_cast<size_t>(embedded->mWidth) * embedded->mHeight * 4);
					for (size_t i = 0; i < static_cast<size_t>(embedded->mWidth) * embedded->mHeight; i++)
					{
						const aiTexel& t = embedded->pcData[i];
						rgba[i * 4 + 0] = t.r; rgba[i * 4 + 1] = t.g; rgba[i * 4 + 2] = t.b; rgba[i * 4 + 3] = t.a;
					}
					texture.width = embedded->mWidth;
					texture.height = embedded->mHeight;
					build_mips(rgba.data(), texture.width, texture.height, srgb, texture.data, texture.mip_count);
				}
				else
				{
					if (embedded)
					{
						encoded.assign(reinterpret_cast<const uint8_t*>(embedded->pcData), reinterpret_cast<const uint8_t*>(embedded->pcData) + embedded->mWidth);
					}
					else if (!read_file(file, encoded))
					{
						seWriteLine("PBR: cannot read texture %s", file.c_str());
						return PBR_NO_TEXTURE;
					}
					if (encoded.size() >= 4 && memcmp(encoded.data(), "DDS ", 4) == 0)
					{
						texture.dds = true;
						texture.may_have_alpha = dds_may_have_alpha(encoded);
						texture.data = std::move(encoded);
					}
					else
					{
						int w = 0, h = 0, channels = 0;
						stbi_uc* pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &w, &h, &channels, 4);
						if (pixels == nullptr)
						{
							seWriteLine("PBR: cannot decode texture %s", file.empty() ? texture_path.C_Str() : file.c_str());
							return PBR_NO_TEXTURE;
						}
						texture.width = static_cast<sg::u32>(w);
						texture.height = static_cast<sg::u32>(h);
						texture.may_have_alpha = channels == 4 || channels == 2;
						build_mips(pixels, texture.width, texture.height, srgb, texture.data, texture.mip_count);
						stbi_image_free(pixels);
					}
				}
				const sg::u32 index = static_cast<sg::u32>(out->textures.size());
				out->textures.push_back(std::move(texture));
				texture_lookup.emplace(key, index);
				return index;
			};

		// --- Materials
		for (unsigned int i = 0; i < scene->mNumMaterials; i++)
		{
			const aiMaterial& m = *scene->mMaterials[i];
			gpu::Material d = {};
			d.base_colour = { 1.0f, 1.0f, 1.0f, 1.0f };
			d.emissive = { 0.0f, 0.0f, 0.0f };
			d.alpha_cutoff = 0.5f;
			d.metallic = 0.0f;
			d.roughness = 0.5f;
			d.normal_scale = 1.0f;
			d.occlusion_strength = 1.0f;
			d.specular = 1.0f;
			d.base_colour_texture = d.normal_texture = d.metal_rough_texture = d.emissive_texture = d.occlusion_texture = d.opacity_texture = PBR_NO_TEXTURE;
			bool alpha_candidate = false;

			d.base_colour_texture = texture_index(m, aiTextureType_BASE_COLOR, true);
			if (d.base_colour_texture == PBR_NO_TEXTURE)
			{
				d.base_colour_texture = texture_index(m, aiTextureType_DIFFUSE, true);
			}

			aiColor4D colour;
			if (is_gltf)
			{
				// glTF factors are linear
				if (m.Get(AI_MATKEY_BASE_COLOR, colour) == aiReturn_SUCCESS)
				{
					d.base_colour = { colour.r, colour.g, colour.b, colour.a };
				}
				float value = 0.0f;
				if (m.Get(AI_MATKEY_METALLIC_FACTOR, value) == aiReturn_SUCCESS) d.metallic = value;
				if (m.Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == aiReturn_SUCCESS) d.roughness = value;
				d.metal_rough_texture = texture_index(m, aiTextureType_METALNESS, false);
				if (d.metal_rough_texture == PBR_NO_TEXTURE)
				{
					d.metal_rough_texture = texture_index(m, aiTextureType_DIFFUSE_ROUGHNESS, false);
				}
				if (d.metal_rough_texture == PBR_NO_TEXTURE)
				{
					// AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE (older assimp glTF importers)
					d.metal_rough_texture = texture_index(m, aiTextureType_UNKNOWN, false);
				}
				d.occlusion_texture = texture_index(m, aiTextureType_AMBIENT_OCCLUSION, false);
				if (d.occlusion_texture == PBR_NO_TEXTURE)
				{
					d.occlusion_texture = texture_index(m, aiTextureType_LIGHTMAP, false);
				}
				aiString alpha_mode;
				if (m.Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode) == aiReturn_SUCCESS && (strcmp(alpha_mode.C_Str(), "MASK") == 0 || strcmp(alpha_mode.C_Str(), "BLEND") == 0))
				{
					d.flags |= PBR_MATERIAL_ALPHA_MASK;
					m.Get(AI_MATKEY_GLTF_ALPHACUTOFF, d.alpha_cutoff);
				}
				float emissive_strength = 1.0f;
				m.Get(AI_MATKEY_EMISSIVE_INTENSITY, emissive_strength);
				if (m.Get(AI_MATKEY_COLOR_EMISSIVE, colour) == aiReturn_SUCCESS)
				{
					d.emissive = { colour.r * emissive_strength, colour.g * emissive_strength, colour.b * emissive_strength };
				}
			}
			else
			{
				// Legacy (OBJ, FBX) colours are display (sRGB) values. A diffuse texture replaces the colour: their diffuse
				// factors are mostly left over Phong tints (Sponza's 0.588 grey would only darken the textures).
				if (d.base_colour_texture == PBR_NO_TEXTURE && (m.Get(AI_MATKEY_BASE_COLOR, colour) == aiReturn_SUCCESS || m.Get(AI_MATKEY_COLOR_DIFFUSE, colour) == aiReturn_SUCCESS))
				{
					d.base_colour = { srgb_to_linear(colour.r), srgb_to_linear(colour.g), srgb_to_linear(colour.b), 1.0f };
				}
				// There is no transparency: mostly transparent surfaces (clear glass, water, ice) are cut out entirely, denser
				// ones (wine, beer) render opaque
				float opacity = 1.0f;
				if (m.Get(AI_MATKEY_OPACITY, opacity) == aiReturn_SUCCESS && opacity < 0.5f)
				{
					d.base_colour.w = 0.0f;
					d.flags |= PBR_MATERIAL_ALPHA_MASK;
				}
				if (specular_is_orm)
				{
					// ORCA scenes (Bistro, Sun Temple, Emerald Square): "specular" holds occlusion, roughness and metallic
					d.metal_rough_texture = texture_index(m, aiTextureType_SPECULAR, false);
					d.metallic = 1.0f;
					d.roughness = 1.0f;
					if (d.metal_rough_texture == PBR_NO_TEXTURE)
					{
						d.metallic = 0.0f;
						d.roughness = 0.6f;
					}
				}
				else
				{
					d.metal_rough_texture = texture_index(m, aiTextureType_SPECULAR, false);
					float shininess = 0.0f;
					if (m.Get(AI_MATKEY_SHININESS, shininess) == aiReturn_SUCCESS && shininess > 0.0f)
					{
						// Blinn-Phong exponent to GGX roughness (alpha = sqrt(2 / (n + 2)), roughness = sqrt(alpha))
						d.roughness = std::clamp(std::sqrt(std::sqrt(2.0f / (shininess + 2.0f))), 0.2f, 1.0f);
					}
					if (d.metal_rough_texture != PBR_NO_TEXTURE)
					{
						d.flags |= PBR_MATERIAL_LEGACY_SPECULAR;
					}
				}
				d.opacity_texture = texture_index(m, aiTextureType_OPACITY, false);
				if (d.opacity_texture != PBR_NO_TEXTURE)
				{
					d.flags |= PBR_MATERIAL_ALPHA_MASK;
				}
				else if (d.base_colour_texture != PBR_NO_TEXTURE && out->textures[d.base_colour_texture].may_have_alpha)
				{
					alpha_candidate = true; // Decided by the GPU alpha analysis once the texture is uploaded
				}
				if (m.Get(AI_MATKEY_COLOR_EMISSIVE, colour) == aiReturn_SUCCESS)
				{
					d.emissive = { srgb_to_linear(colour.r), srgb_to_linear(colour.g), srgb_to_linear(colour.b) };
				}
			}

			d.normal_texture = texture_index(m, aiTextureType_NORMALS, false);
			if (d.normal_texture == PBR_NO_TEXTURE)
			{
				// OBJ files store normal maps as "bump" maps (Sponza's *_ddn); real height maps are not supported
				aiString bump;
				if (m.GetTexture(aiTextureType_HEIGHT, 0, &bump) == aiReturn_SUCCESS)
				{
					const std::string name = lower(bump.C_Str());
					if (name.find("ddn") != std::string::npos || name.find("normal") != std::string::npos || name.find("nrm") != std::string::npos)
					{
						d.normal_texture = texture_index(m, aiTextureType_HEIGHT, false);
					}
				}
			}
			d.emissive_texture = texture_index(m, aiTextureType_EMISSIVE, true);
			if (d.emissive_texture != PBR_NO_TEXTURE && d.emissive.x == 0.0f && d.emissive.y == 0.0f && d.emissive.z == 0.0f)
			{
				d.emissive = { 1.0f, 1.0f, 1.0f };
			}

			int two_sided = 0;
			if (m.Get(AI_MATKEY_TWOSIDED, two_sided) == aiReturn_SUCCESS && two_sided != 0)
			{
				d.flags |= PBR_MATERIAL_DOUBLE_SIDED;
			}
			if (d.flags & PBR_MATERIAL_ALPHA_MASK)
			{
				d.flags |= PBR_MATERIAL_DOUBLE_SIDED; // Cut outs are leaves, cards and fabric seen from both sides
			}
			out->materials.push_back(d);
			out->material_alpha_candidate.push_back(alpha_candidate);
		}
		{
			sg::u32 emissive = 0, normal = 0, metal_rough = 0, base = 0;
			for (const gpu::Material& m : out->materials)
			{
				emissive += (m.emissive_texture != PBR_NO_TEXTURE || m.emissive.x + m.emissive.y + m.emissive.z > 0.0f) ? 1u : 0u;
				normal += m.normal_texture != PBR_NO_TEXTURE ? 1u : 0u;
				metal_rough += m.metal_rough_texture != PBR_NO_TEXTURE ? 1u : 0u;
				base += m.base_colour_texture != PBR_NO_TEXTURE ? 1u : 0u;
			}
			seWriteLine("PBR: %u materials: %u base colour, %u normal, %u metal / roughness textures, %u emissive", scene->mNumMaterials, base, normal, metal_rough, emissive);
		}
		if (out->materials.empty())
		{
			gpu::Material d = {};
			d.base_colour = { 0.8f, 0.8f, 0.8f, 1.0f };
			d.roughness = 0.5f;
			d.normal_scale = d.occlusion_strength = d.specular = 1.0f;
			d.alpha_cutoff = 0.5f;
			d.base_colour_texture = d.normal_texture = d.metal_rough_texture = d.emissive_texture = d.occlusion_texture = d.opacity_texture = PBR_NO_TEXTURE;
			out->materials.push_back(d);
			out->material_alpha_candidate.push_back(false);
		}

		// --- Meshes and meshlets
		std::vector<sg::u32> mesh_lookup(scene->mNumMeshes, ~0u);
		for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; mesh_index++)
		{
			const aiMesh& am = *scene->mMeshes[mesh_index];
			if (!(am.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) || am.mNumVertices == 0 || am.mNumFaces == 0)
			{
				continue;
			}
			gpu::Mesh mesh = {};
			mesh.vertex_offset = static_cast<sg::u32>(out->vertices.size());
			mesh.vertex_count = am.mNumVertices;
			mesh.index_offset = static_cast<sg::u32>(out->indices.size());
			mesh.material = std::min<sg::u32>(am.mMaterialIndex, static_cast<sg::u32>(out->materials.size()) - 1);

			XMFLOAT3 lo = { FLT_MAX, FLT_MAX, FLT_MAX }, hi = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
			for (unsigned int v = 0; v < am.mNumVertices; v++)
			{
				gpu::Vertex vertex = {};
				vertex.position = { am.mVertices[v].x, am.mVertices[v].y, am.mVertices[v].z };
				vertex.normal = am.mNormals ? XMFLOAT3(am.mNormals[v].x, am.mNormals[v].y, am.mNormals[v].z) : XMFLOAT3(0, 1, 0);
				if (am.mTangents && am.mBitangents)
				{
					const XMVECTOR n = XMLoadFloat3(&vertex.normal);
					const XMVECTOR t = XMVectorSet(am.mTangents[v].x, am.mTangents[v].y, am.mTangents[v].z, 0.0f);
					const XMVECTOR b = XMVectorSet(am.mBitangents[v].x, am.mBitangents[v].y, am.mBitangents[v].z, 0.0f);
					const float sign = XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), b)) < 0.0f ? -1.0f : 1.0f;
					vertex.tangent = { am.mTangents[v].x, am.mTangents[v].y, am.mTangents[v].z, sign };
				}
				vertex.uv = am.mTextureCoords[0] ? XMFLOAT2(am.mTextureCoords[0][v].x, am.mTextureCoords[0][v].y) : XMFLOAT2(0, 0);
				lo = { std::min(lo.x, vertex.position.x), std::min(lo.y, vertex.position.y), std::min(lo.z, vertex.position.z) };
				hi = { std::max(hi.x, vertex.position.x), std::max(hi.y, vertex.position.y), std::max(hi.z, vertex.position.z) };
				out->vertices.push_back(vertex);
			}

			std::vector<sg::u32> indices;
			indices.reserve(static_cast<size_t>(am.mNumFaces) * 3);
			for (unsigned int f = 0; f < am.mNumFaces; f++)
			{
				if (am.mFaces[f].mNumIndices != 3)
				{
					continue;
				}
				indices.push_back(am.mFaces[f].mIndices[0]);
				indices.push_back(am.mFaces[f].mIndices[1]);
				indices.push_back(am.mFaces[f].mIndices[2]);
			}
			if (indices.empty())
			{
				out->vertices.resize(mesh.vertex_offset);
				continue;
			}
			meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), am.mNumVertices);
			mesh.index_count = static_cast<sg::u32>(indices.size());
			// Average texel density: alpha tested ray hits pick a texture level from it without fetching positions
			{
				double uv_area = 0.0, area = 0.0;
				for (size_t i = 0; i + 2 < indices.size(); i += 3)
				{
					const gpu::Vertex& a = out->vertices[mesh.vertex_offset + indices[i]];
					const gpu::Vertex& b = out->vertices[mesh.vertex_offset + indices[i + 1]];
					const gpu::Vertex& c = out->vertices[mesh.vertex_offset + indices[i + 2]];
					const XMVECTOR pa = XMLoadFloat3(&a.position), pb = XMLoadFloat3(&b.position), pc = XMLoadFloat3(&c.position);
					area += XMVectorGetX(XMVector3Length(XMVector3Cross(XMVectorSubtract(pb, pa), XMVectorSubtract(pc, pa))));
					uv_area += std::abs((b.uv.x - a.uv.x) * (c.uv.y - a.uv.y) - (b.uv.y - a.uv.y) * (c.uv.x - a.uv.x));
				}
				mesh.uv_density = area > 0.0 ? static_cast<float>(uv_area / area) : 1.0f;
			}

			// Meshlets (vertex indices made absolute, so mesh shaders read the vertex buffer directly)
			const float* positions = &out->vertices[mesh.vertex_offset].position.x;
			const size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), PBR_MESHLET_MAX_VERTICES, PBR_MESHLET_MAX_TRIANGLES);
			std::vector<meshopt_Meshlet> meshlets(max_meshlets);
			std::vector<unsigned int> meshlet_vertices(max_meshlets * PBR_MESHLET_MAX_VERTICES);
			std::vector<unsigned char> meshlet_triangles(max_meshlets * PBR_MESHLET_MAX_TRIANGLES * 3);
			const size_t meshlet_count = meshopt_buildMeshlets(meshlets.data(), meshlet_vertices.data(), meshlet_triangles.data(), indices.data(), indices.size(),
				positions, am.mNumVertices, sizeof(gpu::Vertex), PBR_MESHLET_MAX_VERTICES, PBR_MESHLET_MAX_TRIANGLES, 0.25f);
			mesh.meshlet_offset = static_cast<sg::u32>(out->meshlets.size());
			mesh.meshlet_count = static_cast<sg::u32>(meshlet_count);
			for (size_t i = 0; i < meshlet_count; i++)
			{
				meshopt_Meshlet& ml = meshlets[i];
				meshopt_optimizeMeshlet(&meshlet_vertices[ml.vertex_offset], &meshlet_triangles[ml.triangle_offset], ml.triangle_count, ml.vertex_count);
				const meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshlet_vertices[ml.vertex_offset], &meshlet_triangles[ml.triangle_offset], ml.triangle_count,
					positions, am.mNumVertices, sizeof(gpu::Vertex));
				gpu::Meshlet g = {};
				g.vertex_offset = static_cast<sg::u32>(out->meshlet_vertices.size());
				g.triangle_offset = static_cast<sg::u32>(out->meshlet_triangles.size());
				g.vertex_count = ml.vertex_count;
				g.triangle_count = ml.triangle_count;
				g.sphere = { bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius };
				g.cone_apex = { bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2], bounds.cone_cutoff };
				g.cone_axis = { bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], 0.0f };
				for (unsigned int v = 0; v < ml.vertex_count; v++)
				{
					out->meshlet_vertices.push_back(mesh.vertex_offset + meshlet_vertices[ml.vertex_offset + v]);
				}
				for (unsigned int t = 0; t < ml.triangle_count; t++)
				{
					const unsigned char* tri = &meshlet_triangles[ml.triangle_offset + t * 3];
					out->meshlet_triangles.push_back(sg::u32(tri[0]) | (sg::u32(tri[1]) << 8) | (sg::u32(tri[2]) << 16));
				}
				out->meshlets.push_back(g);
			}
			out->indices.insert(out->indices.end(), indices.begin(), indices.end());

			const XMFLOAT3 centre = { (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
			const float radius = 0.5f * std::sqrt((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y) + (hi.z - lo.z) * (hi.z - lo.z));
			mesh.sphere = { centre.x, centre.y, centre.z, radius };
			mesh.aabb_min = { lo.x, lo.y, lo.z, 0.0f };
			mesh.aabb_max = { hi.x, hi.y, hi.z, 0.0f };
			mesh_lookup[mesh_index] = static_cast<sg::u32>(out->meshes.size());
			out->meshes.push_back(mesh);
		}

		// --- Instances: one per mesh of every node, with the node's world transform (scaled to metres)
		struct NodeEntry { const aiNode* node; XMMATRIX parent; };
		std::vector<NodeEntry> stack = { { scene->mRootNode, XMMatrixScaling(scale, scale, scale) } };
		while (!stack.empty())
		{
			const NodeEntry entry = stack.back();
			stack.pop_back();
			const XMMATRIX world = XMMatrixMultiply(to_xm(entry.node->mTransformation), entry.parent);
			for (unsigned int i = 0; i < entry.node->mNumMeshes; i++)
			{
				const sg::u32 mesh_index = mesh_lookup[entry.node->mMeshes[i]];
				if (mesh_index == ~0u)
				{
					continue;
				}
				const gpu::Mesh& mesh = out->meshes[mesh_index];
				gpu::Instance instance = {};
				XMStoreFloat4x4(&instance.world, world);
				XMStoreFloat4x4(&instance.previous_world, world);
				XMMATRIX normal_matrix = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
				normal_matrix.r[3] = XMVectorSet(0, 0, 0, 1);
				XMStoreFloat4x4(&instance.normal_matrix, normal_matrix);
				instance.mesh = mesh_index;
				instance.material = mesh.material;
				if (XMVectorGetX(XMMatrixDeterminant(world)) < 0.0f)
				{
					instance.flags |= PBR_INSTANCE_MIRRORED;
				}

				// World bounds from the transformed box corners
				XMFLOAT3 lo = { FLT_MAX, FLT_MAX, FLT_MAX }, hi = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
				for (int c = 0; c < 8; c++)
				{
					const XMVECTOR corner = XMVectorSet(c & 1 ? mesh.aabb_max.x : mesh.aabb_min.x, c & 2 ? mesh.aabb_max.y : mesh.aabb_min.y, c & 4 ? mesh.aabb_max.z : mesh.aabb_min.z, 1.0f);
					XMFLOAT3 p;
					XMStoreFloat3(&p, XMVector3TransformCoord(corner, world));
					lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
					hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
				}
				const float radius = 0.5f * std::sqrt((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y) + (hi.z - lo.z) * (hi.z - lo.z));
				instance.world_sphere = { (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f, radius };
				out->bounds_min = { std::min(out->bounds_min.x, lo.x), std::min(out->bounds_min.y, lo.y), std::min(out->bounds_min.z, lo.z) };
				out->bounds_max = { std::max(out->bounds_max.x, hi.x), std::max(out->bounds_max.y, hi.y), std::max(out->bounds_max.z, hi.z) };
				out->instances.push_back(instance);
			}
			for (unsigned int i = 0; i < entry.node->mNumChildren; i++)
			{
				stack.push_back({ entry.node->mChildren[i], world });
			}
		}
		if (out->instances.empty())
		{
			out->error = "the scene has no triangle meshes";
		}
		// Emissive triangles for light sampling, weighted by emitted power (material emission luminance * world area; the
		// texture modulates the sample but not the weight, so shaders can recompute a hit's sampling probability)
		{
			std::vector<double> weights;
			for (sg::u32 instance_index = 0; instance_index < out->instances.size(); instance_index++)
			{
				const gpu::Instance& instance = out->instances[instance_index];
				const gpu::Material& material = out->materials[instance.material];
				const double luminance = 0.2126 * material.emissive.x + 0.7152 * material.emissive.y + 0.0722 * material.emissive.z;
				if (luminance <= 0.0)
				{
					continue;
				}
				const gpu::Mesh& mesh = out->meshes[instance.mesh];
				const XMMATRIX world = XMLoadFloat4x4(&instance.world);
				for (sg::u32 t = 0; t < mesh.index_count / 3; t++)
				{
					XMVECTOR p[3];
					for (int k = 0; k < 3; k++)
					{
						p[k] = XMVector3TransformCoord(XMLoadFloat3(&out->vertices[mesh.vertex_offset + out->indices[mesh.index_offset + t * 3 + k]].position), world);
					}
					const double area = 0.5 * XMVectorGetX(XMVector3Length(XMVector3Cross(XMVectorSubtract(p[1], p[0]), XMVectorSubtract(p[2], p[0]))));
					if (area <= 0.0)
					{
						continue;
					}
					gpu::EmissiveTriangle e = {};
					e.instance = instance_index;
					e.primitive = t;
					out->emissive_triangles.push_back(e);
					weights.push_back(luminance * area);
				}
			}
			// Vose's alias method
			const size_t count = weights.size();
			double total = 0.0;
			for (double w : weights) total += w;
			out->emissive_weight = total;
			if (count > 0 && total > 0.0)
			{
				std::vector<double> scaled(count);
				std::vector<sg::u32> below, above; // Columns under / over the average weight
				for (size_t i = 0; i < count; i++)
				{
					scaled[i] = weights[i] * count / total;
					(scaled[i] < 1.0 ? below : above).push_back(static_cast<sg::u32>(i));
				}
				while (!below.empty() && !above.empty())
				{
					const sg::u32 s = below.back();
					below.pop_back();
					const sg::u32 l = above.back();
					out->emissive_triangles[s].probability = static_cast<float>(scaled[s]);
					out->emissive_triangles[s].alias = l;
					scaled[l] = (scaled[l] + scaled[s]) - 1.0;
					if (scaled[l] < 1.0)
					{
						above.pop_back();
						below.push_back(l);
					}
				}
				for (sg::u32 i : above) { out->emissive_triangles[i].probability = 1.0f; out->emissive_triangles[i].alias = i; }
				for (sg::u32 i : below) { out->emissive_triangles[i].probability = 1.0f; out->emissive_triangles[i].alias = i; }
			}
		}

		// The file's first camera, in world space
		if (scene->mNumCameras > 0)
		{
			const aiCamera& camera = *scene->mCameras[0];
			if (const aiNode* camera_node = scene->mRootNode->FindNode(camera.mName))
			{
				XMMATRIX world = XMMatrixIdentity();
				for (const aiNode* n = camera_node; n != nullptr; n = n->mParent)
				{
					world = XMMatrixMultiply(world, to_xm(n->mTransformation));
				}
				world = XMMatrixMultiply(world, XMMatrixScaling(scale, scale, scale));
				const XMVECTOR position = XMVector3TransformCoord(XMVectorSet(camera.mPosition.x, camera.mPosition.y, camera.mPosition.z, 1.0f), world);
				// aiProcess_ConvertToLeftHanded mirrors the node transforms but not the camera's local look direction, which
				// then points backwards (checked against the views the Bistro and Sun Temple files were authored with)
				const XMVECTOR forward = XMVectorNegate(XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(camera.mLookAt.x, camera.mLookAt.y, camera.mLookAt.z, 0.0f), world)));
				XMStoreFloat3(&out->camera_position, position);
				XMStoreFloat3(&out->camera_target, XMVectorAdd(position, forward));
				out->has_camera = true;
			}
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
		for (GPUTexture& t : textures)
		{
			if (t.bindless != ~0u)
			{
				device->free_bindless_descriptor(t.bindless);
			}
		}
		textures.clear();
		buffers.clear();
		vertex_buffer = nullptr;
		index_buffer = nullptr;
		pending_uploads.clear();
		blas.clear();
		blas_pools.clear();
		blas_pool_used = 0;
		blas_pool_capacity = 0;
		tlas = nullptr;
		tlas_instances = nullptr;
		scratch = nullptr;
		material_buffer = nullptr;
		views = {};
		materials.clear();
		instances.clear();
		alpha_candidates.clear();
		imported = nullptr;
		statistics = {};
		has_file_camera = false;
		emissive_triangle_count = 0;
		emissive_weight = 0.0f;
		next_texture = 0;
		next_blas = 0;
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
		error.clear();
		state = State::Importing;
		const ScenePreset copy = new_preset ? *new_preset : ScenePreset{};
		seWriteLine("PBR: importing %s", path.c_str());
		import_future = std::async(std::launch::async, &Scene::import, path, copy, new_preset != nullptr);
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
		const size_t bytes = std::max<size_t>(data.size() * sizeof(T), sizeof(T));
		sg::SharedPtr<sg::Buffer> buffer = sg::create_buffer(*device, static_cast<sg::u32>(sg::AlignUp<size_t>(bytes, 256)), sg::BufferType::GeneralDataBuffer, false);
		srv = device->create_shader_resource_view(buffer, sizeof(T), std::max<size_t>(data.size(), 1));
		if (!data.empty())
		{
			PendingUpload upload;
			upload.buffer = buffer;
			upload.data = std::make_shared<std::vector<uint8_t>>(reinterpret_cast<const uint8_t*>(data.data()), reinterpret_cast<const uint8_t*>(data.data()) + data.size() * sizeof(T));
			pending_uploads.push_back(std::move(upload));
		}
		buffers.push_back(buffer);
		return buffer;
	}

	bool Scene::upload_pending_buffers(sg::Ptr<sg::UploadHeap>& upload_heap)
	{
		sg::u64 budget = kUploadBudget;
		while (!pending_uploads.empty() && budget > 0)
		{
			PendingUpload& upload = pending_uploads.front();
			const size_t remaining = upload.data->size() - upload.offset;
			const sg::u32 chunk = static_cast<sg::u32>(std::min<size_t>(remaining, budget));
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

	void Scene::begin_upload(sg::Ptr<sg::UploadHeap>&)
	{
		ImportedScene& s = *imported;
		statistics.import_seconds = s.seconds;
		statistics.meshes = static_cast<sg::u32>(s.meshes.size());
		statistics.vertices = static_cast<sg::u32>(s.vertices.size());
		statistics.materials = static_cast<sg::u32>(s.materials.size());
		statistics.textures = static_cast<sg::u32>(s.textures.size());
		statistics.instances = static_cast<sg::u32>(s.instances.size());
		bounds_min = s.bounds_min;
		bounds_max = s.bounds_max;
		has_file_camera = s.has_camera;
		file_camera_position = s.camera_position;
		file_camera_target = s.camera_target;
		if (has_file_camera)
		{
			seWriteLine("PBR: file camera at (%.2f %.2f %.2f) looking at (%.2f %.2f %.2f)", file_camera_position.x, file_camera_position.y, file_camera_position.z,
				file_camera_target.x, file_camera_target.y, file_camera_target.z);
		}

		// Meshlet visibility bits and amplification task counts per instance
		sg::u32 bit = 0;
		max_tasks = 0;
		statistics.meshlets = 0;
		statistics.triangles = 0;
		for (gpu::Instance& instance : s.instances)
		{
			const gpu::Mesh& mesh = s.meshes[instance.mesh];
			instance.meshlet_visibility_offset = bit;
			bit += mesh.meshlet_count;
			max_tasks += (mesh.meshlet_count + PBR_TASK_MESHLETS - 1) / PBR_TASK_MESHLETS;
			statistics.meshlets += mesh.meshlet_count;
			statistics.triangles += mesh.index_count / 3;
		}
		meshlet_visibility_bits = bit;

		vertex_buffer = create_buffer(s.vertices, views.vertices);
		index_buffer = create_buffer(s.indices, views.indices);
		create_buffer(s.meshlets, views.meshlets);
		create_buffer(s.meshlet_vertices, views.meshlet_vertices);
		create_buffer(s.meshlet_triangles, views.meshlet_triangles);
		create_buffer(s.meshes, views.meshes);
		create_buffer(s.emissive_triangles, views.emissive_triangles);
		emissive_triangle_count = static_cast<sg::u32>(s.emissive_triangles.size());
		emissive_weight = static_cast<float>(s.emissive_weight);
		statistics.emissive_triangles = emissive_triangle_count;
		materials = s.materials;
		instances = s.instances;
		// Freed as soon as uploaded (the pending uploads hold copies)
		std::vector<gpu::Vertex>().swap(s.vertices);
		std::vector<sg::u32>().swap(s.meshlet_vertices);
		std::vector<sg::u32>().swap(s.meshlet_triangles);
		textures.resize(s.textures.size());
		seWriteLine("PBR: imported %s in %.1f s: %u instances, %u meshes, %u meshlets, %u triangles, %u materials, %u textures, bounds (%.1f %.1f %.1f) - (%.1f %.1f %.1f)",
			path.c_str(), s.seconds, statistics.instances, statistics.meshes, statistics.meshlets, statistics.triangles, statistics.materials, statistics.textures,
			bounds_min.x, bounds_min.y, bounds_min.z, bounds_max.x, bounds_max.y, bounds_max.z);
	}

	bool Scene::upload_textures(sg::Ptr<sg::UploadHeap>& upload_heap)
	{
		sg::u64 budget = kUploadBudget;
		while (next_texture < imported->textures.size())
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
				size_t offset = 0;
				sg::u32 w = source.width, h = source.height;
				for (sg::u32 mip = 0; mip < source.mip_count; mip++)
				{
					upload_heap->upload_texture_subresource(gpu_texture.texture.get(), mip, source.data.data() + offset, w * 4, h);
					offset += static_cast<size_t>(w) * h * 4;
					w = std::max(1u, w / 2);
					h = std::max(1u, h / 2);
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
			next_texture++;
			if (budget == 0)
			{
				break;
			}
		}
		return next_texture == imported->textures.size();
	}

	void Scene::analyse_alpha(sg::CommandList& command_list, SimpleLinearConstantBuffer& cbuffer)
	{
		// Bindless texture indices; materials whose textures failed to load lose them
		auto remap = [&](sg::u32& texture)
			{
				if (texture != PBR_NO_TEXTURE)
				{
					texture = (texture < textures.size() && textures[texture].bindless != ~0u) ? textures[texture].bindless : PBR_NO_TEXTURE;
				}
			};
		// Colour textures loaded from DDS files in UNORM formats hold sRGB values: shaders decode them (RGBA8 textures decoded
		// from other files are created with _SRGB formats)
		auto srgb_flag = [&](gpu::Material& m, sg::u32 texture, sg::u32 flag)
			{
				if (texture == PBR_NO_TEXTURE || texture >= textures.size() || !textures[texture].texture || !imported->textures[texture].dds)
				{
					return;
				}
				const DXGI_FORMAT format = textures[texture].texture->resource_create_desc.format;
				const bool srgb_format = format == DXGI_FORMAT_BC1_UNORM_SRGB || format == DXGI_FORMAT_BC2_UNORM_SRGB || format == DXGI_FORMAT_BC3_UNORM_SRGB ||
					format == DXGI_FORMAT_BC7_UNORM_SRGB || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
				if (!srgb_format)
				{
					m.flags |= flag;
				}
			};
		for (size_t i = 0; i < materials.size(); i++)
		{
			gpu::Material& m = materials[i];
			srgb_flag(m, m.base_colour_texture, PBR_MATERIAL_BASE_COLOUR_SRGB);
			srgb_flag(m, m.emissive_texture, PBR_MATERIAL_EMISSIVE_SRGB);
			remap(m.base_colour_texture);
			remap(m.normal_texture);
			remap(m.metal_rough_texture);
			remap(m.emissive_texture);
			remap(m.occlusion_texture);
			remap(m.opacity_texture);
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
		if (!alpha_analysis_pipeline)
		{
			sg::BindingDesc binding;
			binding.cbv_binding_count = 1;
			binding.uav_binding_count = 1;
			binding.sampler_binding_count = 6;
			binding.bindless = true;
			sg::PipelineDesc::Compute desc;
			desc.compute_shader = sg::create_compute_shader(*device, get_shader_path("PBR_AlphaAnalysis"));
			alpha_analysis_pipeline = device->create_pipeline(desc, binding);
		}
		const sg::u32 count_bytes = static_cast<sg::u32>(sg::AlignUp<size_t>(alpha_candidates.size() * 4, 256));
		alpha_counts = sg::create_buffer(*device, count_bytes, sg::BufferType::GeneralDataBuffer, true);
		alpha_counts_uav = device->create_unordered_access_view(alpha_counts, 4, alpha_candidates.size());
		alpha_readback = sg::create_readback_buffer(*device, count_bytes);
		sg::ShaderResourceView no_srv = {};
		command_list.clear_buffer_uint(alpha_counts_uav, no_srv, 0);

		command_list.set_pipeline(alpha_analysis_pipeline.get());
		for (size_t i = 0; i < alpha_candidates.size(); i++)
		{
			struct Constants
			{
				sg::u32 texture;
				sg::u32 output;
				float cutoff;
				sg::u32 pad;
			} constants = { materials[alpha_candidates[i]].base_colour_texture, static_cast<sg::u32>(i), materials[alpha_candidates[i]].alpha_cutoff, 0 };
			sg::Binding binding;
			binding.cbv_binding_count = 1;
			binding.uav_binding_count = 1;
			binding.sampler_binding_count = 6;
			sg::ConstantBufferView cbv = cbuffer.AllocateAndWrite(constants);
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
		if (!alpha_candidates.empty())
		{
			std::vector<sg::u32> counts(alpha_candidates.size());
			alpha_readback->read_memory(0, counts.data(), counts.size() * 4);
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
		}
		statistics.alpha_tested_materials = 0;
		for (const gpu::Material& m : materials)
		{
			statistics.alpha_tested_materials += (m.flags & PBR_MATERIAL_ALPHA_MASK) ? 1u : 0u;
		}
		sg::u64 alpha_triangles = 0;
		for (gpu::Instance& instance : instances)
		{
			const sg::u32 flags = materials[instance.material].flags;
			if (flags & (PBR_MATERIAL_ALPHA_MASK | PBR_MATERIAL_DOUBLE_SIDED))
			{
				instance.flags |= PBR_INSTANCE_BUCKET_MASK; // PBR_BUCKET_MASKED
			}
			if (flags & PBR_MATERIAL_ALPHA_MASK)
			{
				alpha_triangles += imported->meshes[instance.mesh].index_count / 3;
			}
		}
		seWriteLine("PBR: %u alpha tested materials, %llu alpha tested triangles", statistics.alpha_tested_materials, alpha_triangles);
		alpha_counts = nullptr;
		alpha_readback = nullptr;
		alpha_counts_uav = {};
		material_buffer = create_buffer(materials, views.materials);
		create_buffer(instances, views.instances);
	}

	void Scene::build_acceleration_structures(sg::CommandList& command_list, sg::Ptr<sg::UploadHeap>& upload_heap)
	{
		const ImportedScene& s = *imported;
		if (blas.empty())
		{
			blas.resize(s.meshes.size());
			// One scratch buffer, sized for the largest build (builds are serialised by the UAV barrier after each)
			sg::u64 scratch_size = device->get_top_level_prebuild_info(static_cast<sg::u32>(instances.size())).scratch_size;
			for (const gpu::Mesh& mesh : s.meshes)
			{
				sg::Raytracing::GeometryDesc geometry;
				geometry.vertex_count = mesh.vertex_count;
				geometry.vertex_stride = sizeof(gpu::Vertex);
				geometry.index_count = mesh.index_count;
				scratch_size = std::max(scratch_size, device->get_bottom_level_prebuild_info(&geometry, 1).scratch_size);
			}
			scratch = sg::create_buffer(*device, static_cast<sg::u32>(scratch_size), sg::BufferType::GeneralDataBuffer, true);
		}

		sg::u32 builds = 0;
		while (next_blas < s.meshes.size() && builds < kBlasBuildsPerFrame)
		{
			const gpu::Mesh& mesh = s.meshes[next_blas];
			sg::Raytracing::GeometryDesc geometry;
			geometry.vertex_buffer = vertex_buffer.get();
			geometry.vertex_offset = static_cast<sg::u64>(mesh.vertex_offset) * sizeof(gpu::Vertex);
			geometry.vertex_stride = sizeof(gpu::Vertex);
			geometry.vertex_count = mesh.vertex_count;
			geometry.index_buffer = index_buffer.get();
			geometry.index_offset = static_cast<sg::u64>(mesh.index_offset) * 4;
			geometry.index_count = mesh.index_count;
			// Alpha tested geometry reports candidate hits, which ray queries test against the opacity texture
			geometry.opaque = (materials[mesh.material].flags & PBR_MATERIAL_ALPHA_MASK) == 0;
			const sg::Raytracing::PrebuildInfo info = device->get_bottom_level_prebuild_info(&geometry, 1);
			// Packed into pool buffers (placed buffers align to 64 KiB, acceleration structures only need 256 bytes)
			if (blas_pools.empty() || blas_pool_used + info.result_size > blas_pool_capacity)
			{
				blas_pool_capacity = std::max<sg::u64>(64ull << 20, info.result_size);
				blas_pools.push_back(sg::create_buffer(*device, static_cast<sg::u32>(blas_pool_capacity), sg::BufferType::AccelerationStructure, true));
				blas_pool_used = 0;
			}
			blas[next_blas] = { static_cast<sg::u32>(blas_pools.size() - 1), blas_pool_used };
			command_list.build_bottom_level_acceleration_structure(&geometry, 1, blas_pools.back().get(), scratch.get(), blas_pool_used);
			blas_pool_used += info.result_size;
			statistics.acceleration_structure_bytes += info.result_size;
			next_blas++;
			builds++;
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
				d.transform[r][0] = instance.world.m[0][r];
				d.transform[r][1] = instance.world.m[1][r];
				d.transform[r][2] = instance.world.m[2][r];
				d.transform[r][3] = instance.world.m[3][r];
			}
			d.instance_id = static_cast<sg::u32>(i);
			d.instance_mask = 0xFF;
			d.contribution_to_hit_group = 0;
			d.flags = sg::Raytracing::InstanceFlagTriangleCullDisable;
			d.acceleration_structure = device->get_gpu_address(blas_pools[blas[instance.mesh].pool].get()) + blas[instance.mesh].offset;
		}
		const sg::u32 instance_bytes = static_cast<sg::u32>(descs.size() * sizeof(sg::Raytracing::InstanceDesc));
		tlas_instances = sg::create_buffer(*device, static_cast<sg::u32>(sg::AlignUp<size_t>(instance_bytes, 256)), sg::BufferType::GeneralDataBuffer, false);
		const sg::UploadHeap::Offset offset = upload_heap->allocate_upload_memory(instance_bytes, 256);
		seAssert(offset != sg::UploadHeap::INVALID_OFFSET, "Upload heap full");
		upload_heap->write_upload_memory(offset, descs.data(), instance_bytes);
		upload_heap->upload_to_buffer(tlas_instances.get(), 0, offset, instance_bytes);

		const sg::Raytracing::PrebuildInfo info = device->get_top_level_prebuild_info(static_cast<sg::u32>(descs.size()));
		tlas = sg::create_buffer(*device, static_cast<sg::u32>(info.result_size), sg::BufferType::AccelerationStructure, true);
		command_list.build_top_level_acceleration_structure(tlas_instances.get(), static_cast<sg::u32>(descs.size()), tlas.get(), scratch.get());
		statistics.acceleration_structure_bytes += info.result_size;
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
			begin_upload(upload_heap);
			state = State::Uploading;
			[[fallthrough]];
		case State::Uploading:
		{
			const bool buffers_done = upload_pending_buffers(upload_heap);
			const bool textures_done = buffers_done && upload_textures(upload_heap);
			if (textures_done)
			{
				analyse_alpha(command_list, cbuffer);
				state = State::AnalysingAlpha;
			}
			return;
		}
		case State::AnalysingAlpha:
			// The frame that recorded the analysis has finished (the application waits for every frame)
			finish_alpha_analysis();
			state = State::BuildingAccelerationStructures;
			return;
		case State::BuildingAccelerationStructures:
			// Material and instance buffers first (the BLAS builds read the vertex and index buffers, already uploaded)
			if (!upload_pending_buffers(upload_heap))
			{
				return;
			}
			build_acceleration_structures(command_list, upload_heap);
			return;
		default:
			return;
		}
	}
}
