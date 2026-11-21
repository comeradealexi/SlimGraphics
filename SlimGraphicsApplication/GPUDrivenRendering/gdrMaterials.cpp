#include "gdrMaterials.h"
#include <assimp\scene.h>
#include <assimp\material.h>
#include <seEngineBasicFileIO.h>
#include <sgUtils.h>
#include "stb_image.h"
#include <algorithm>

namespace gdr
{
	namespace
	{
		// Texture paths in model files are relative to the model's folder
		std::string resolve_texture_path(const std::string& model_path, const char* texture_path)
		{
			const size_t slash = model_path.find_last_of("\\/");
			return slash == std::string::npos ? std::string(texture_path) : model_path.substr(0, slash + 1) + texture_path;
		}

		// Resamples an RGBA8 image to size x size: area average when shrinking, bilinear when enlarging
		std::vector<uint8_t> resample(const uint8_t* src, sg::u32 width, sg::u32 height, sg::u32 size)
		{
			std::vector<uint8_t> dst(static_cast<size_t>(size) * size * 4);
			for (sg::u32 y = 0; y < size; y++)
			{
				for (sg::u32 x = 0; x < size; x++)
				{
					uint8_t* out = &dst[(static_cast<size_t>(y) * size + x) * 4];
					if (width >= size && height >= size)
					{
						const sg::u32 x0 = x * width / size, x1 = std::max(x0 + 1, (x + 1) * width / size);
						const sg::u32 y0 = y * height / size, y1 = std::max(y0 + 1, (y + 1) * height / size);
						sg::u32 sum[4] = {};
						for (sg::u32 sy = y0; sy < y1; sy++)
						{
							for (sg::u32 sx = x0; sx < x1; sx++)
							{
								const uint8_t* p = &src[(static_cast<size_t>(sy) * width + sx) * 4];
								for (int c = 0; c < 4; c++) sum[c] += p[c];
							}
						}
						const sg::u32 count = (x1 - x0) * (y1 - y0);
						for (int c = 0; c < 4; c++) out[c] = static_cast<uint8_t>((sum[c] + count / 2) / count);
					}
					else
					{
						const float fx = std::max(0.0f, (x + 0.5f) * width / size - 0.5f), fy = std::max(0.0f, (y + 0.5f) * height / size - 0.5f);
						const sg::u32 x0 = std::min(static_cast<sg::u32>(fx), width - 1), y0 = std::min(static_cast<sg::u32>(fy), height - 1);
						const sg::u32 x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
						const float tx = fx - x0, ty = fy - y0;
						for (int c = 0; c < 4; c++)
						{
							auto texel = [&](sg::u32 px, sg::u32 py) { return static_cast<float>(src[(static_cast<size_t>(py) * width + px) * 4 + c]); };
							const float top = texel(x0, y0) + (texel(x1, y0) - texel(x0, y0)) * tx;
							const float bottom = texel(x0, y1) + (texel(x1, y1) - texel(x0, y1)) * tx;
							out[c] = static_cast<uint8_t>(std::clamp(top + (bottom - top) * ty + 0.5f, 0.0f, 255.0f));
						}
					}
				}
			}
			return dst;
		}

		sg::u32 mip_count_for(sg::u32 size)
		{
			sg::u32 count = 1;
			while (size > 1) { size /= 2; count++; }
			return count;
		}
	}

	sg::u32 MaterialTextures::add_rgba8(const std::string& key, const uint8_t* rgba, sg::u32 width, sg::u32 height)
	{
		seAssert(!texture, "Textures must be added before the texture array is created");
		if (const auto it = slice_lookup.find(key); it != slice_lookup.end())
		{
			return it->second;
		}
		if (!rgba || width == 0 || height == 0)
		{
			return slice_lookup[key] = GDR_NO_TEXTURE;
		}
		Slice slice;
		slice.pixels = (width == kSliceSize && height == kSliceSize) ? std::vector<uint8_t>(rgba, rgba + static_cast<size_t>(width) * height * 4) : resample(rgba, width, height, kSliceSize);
		for (size_t i = 3; i < slice.pixels.size() && !slice.cutout_alpha; i += 4)
		{
			slice.cutout_alpha = slice.pixels[i] < 128;
		}
		slices.push_back(std::move(slice));
		return slice_lookup[key] = static_cast<sg::u32>(slices.size() - 1);
	}

	sg::u32 MaterialTextures::add_encoded(const std::string& key, const uint8_t* data, size_t size)
	{
		if (const auto it = slice_lookup.find(key); it != slice_lookup.end())
		{
			return it->second;
		}
		int width = 0, height = 0, channels = 0;
		stbi_uc* rgba = data && size ? stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4) : nullptr;
		if (!rgba)
		{
			seWriteLine("GDR: cannot decode texture %s (supported: PNG, JPEG, TGA, BMP, PSD, GIF, HDR, PIC, PNM)", key.c_str());
			return slice_lookup[key] = GDR_NO_TEXTURE;
		}
		const sg::u32 slice = add_rgba8(key, rgba, static_cast<sg::u32>(width), static_cast<sg::u32>(height));
		stbi_image_free(rgba);
		return slice;
	}

	sg::u32 MaterialTextures::add_file(const std::string& path)
	{
		if (const auto it = slice_lookup.find(path); it != slice_lookup.end())
		{
			return it->second;
		}
		const std::vector<uint8_t> file = se::BasicFileIO::load_file(path.c_str());
		if (file.empty())
		{
			seWriteLine("GDR: texture not found %s", path.c_str());
			return slice_lookup[path] = GDR_NO_TEXTURE;
		}
		seWriteLine("GDR: loading texture %s", path.c_str());
		return add_encoded(path, file.data(), file.size());
	}

	void MaterialTextures::upload(sg::SharedPtr<sg::Device>& device, sg::Ptr<sg::UploadHeap>& upload_heap)
	{
		if (!texture)
		{
			if (slices.empty())
			{
				// Nothing textured: a white slice keeps the binding valid
				Slice white;
				white.pixels.assign(static_cast<size_t>(kSliceSize) * kSliceSize * 4, 255);
				slices.push_back(std::move(white));
			}
			sg::ResourceCreateDesc desc(kSliceSize, kSliceSize, DXGI_FORMAT_R8G8B8A8_UNORM);
			desc.dimension = sg::ResourceDimension::Texture2DArray;
			desc.depth = static_cast<sg::u32>(slices.size());
			desc.mip_count = mip_count_for(kSliceSize);
			texture = sg::create_texture(*device, desc);
			srv = device->create_shader_resource_view(texture);
			next_upload = 0;
		}

		const sg::u32 mip_count = texture->resource_create_desc.mip_count;
		sg::u32 uploaded_bytes = 0;
		while (next_upload < slices.size() && (uploaded_bytes == 0 || uploaded_bytes + kSliceSize * kSliceSize * 6 <= kUploadBudget))
		{
			// Box filtered mip chain, built while uploading so only one slice's mips exist at a time
			std::vector<uint8_t> current = std::move(slices[next_upload].pixels);
			sg::u32 size = kSliceSize;
			for (sg::u32 mip = 0; mip < mip_count; mip++)
			{
				const sg::u32 subresource = mip + static_cast<sg::u32>(next_upload) * mip_count;
				upload_heap->upload_texture_subresource(texture.get(), subresource, current.data(), size * 4, size);
				uploaded_bytes += sg::AlignUp(size * 4, 256u) * size;
				if (size == 1)
				{
					break;
				}
				const sg::u32 half = size / 2;
				std::vector<uint8_t> next(static_cast<size_t>(half) * half * 4);
				for (sg::u32 y = 0; y < half; y++)
				{
					for (sg::u32 x = 0; x < half; x++)
					{
						for (int c = 0; c < 4; c++)
						{
							auto texel = [&](sg::u32 px, sg::u32 py) { return static_cast<sg::u32>(current[(static_cast<size_t>(py) * size + px) * 4 + c]); };
							next[(static_cast<size_t>(y) * half + x) * 4 + c] = static_cast<uint8_t>((texel(x * 2, y * 2) + texel(x * 2 + 1, y * 2) + texel(x * 2, y * 2 + 1) + texel(x * 2 + 1, y * 2 + 1) + 2) / 4);
						}
					}
				}
				current.swap(next);
				size = half;
			}
			next_upload++;
		}
	}

	ShaderStructs::MaterialData default_material()
	{
		ShaderStructs::MaterialData d = {};
		d.texture_diffuse_index = d.texture_specular_index = d.texture_normals_index = d.texture_opacity_index = GDR_NO_TEXTURE;
		d.diffuse_colour = ShaderStructs::float4(1.0f, 1.0f, 1.0f, 1.0f);
		d.specular = ShaderStructs::float4(0.04f, 0.04f, 0.04f, 32.0f);
		d.flags = 0;
		d.bump_strength = 3.0f;
		return d;
	}

	ShaderStructs::MaterialData load_material(const aiScene& scene, const aiMaterial& material, const std::string& model_path, MaterialTextures& textures)
	{
		ShaderStructs::MaterialData d = default_material();

		// Assimp gives files without materials its grey default material; those render with the node colour alone
		aiColor4D colour;
		if (material.GetName() != aiString(AI_DEFAULT_MATERIAL_NAME))
		{
			if (material.Get(AI_MATKEY_BASE_COLOR, colour) == aiReturn_SUCCESS || material.Get(AI_MATKEY_COLOR_DIFFUSE, colour) == aiReturn_SUCCESS)
			{
				d.diffuse_colour = ShaderStructs::float4(colour.r, colour.g, colour.b, 1.0f);
			}
			if (material.Get(AI_MATKEY_COLOR_SPECULAR, colour) == aiReturn_SUCCESS)
			{
				d.specular = ShaderStructs::float4(colour.r, colour.g, colour.b, d.specular.w);
			}
			float shininess = 0.0f;
			if (material.Get(AI_MATKEY_SHININESS, shininess) == aiReturn_SUCCESS && shininess > 0.0f)
			{
				d.specular.w = std::clamp(shininess, 2.0f, 512.0f);
			}
		}

		auto slice_of = [&](aiTextureType type) -> sg::u32
			{
				aiString path;
				if (material.GetTexture(type, 0, &path) != aiReturn_SUCCESS || path.length == 0)
				{
					return GDR_NO_TEXTURE;
				}
				// Embedded textures ("*0" or a file name stored in the model): compressed data, or raw BGRA8 texels
				if (const aiTexture* embedded = scene.GetEmbeddedTexture(path.C_Str()))
				{
					const std::string key = model_path + "#" + path.C_Str();
					if (embedded->mHeight == 0)
					{
						return textures.add_encoded(key, reinterpret_cast<const uint8_t*>(embedded->pcData), embedded->mWidth);
					}
					std::vector<uint8_t> rgba(static_cast<size_t>(embedded->mWidth) * embedded->mHeight * 4);
					for (size_t i = 0; i < static_cast<size_t>(embedded->mWidth) * embedded->mHeight; i++)
					{
						const aiTexel& t = embedded->pcData[i];
						rgba[i * 4 + 0] = t.r; rgba[i * 4 + 1] = t.g; rgba[i * 4 + 2] = t.b; rgba[i * 4 + 3] = t.a;
					}
					return textures.add_rgba8(key, rgba.data(), embedded->mWidth, embedded->mHeight);
				}
				return textures.add_file(resolve_texture_path(model_path, path.C_Str()));
			};

		d.texture_diffuse_index = slice_of(aiTextureType_BASE_COLOR);
		if (d.texture_diffuse_index == GDR_NO_TEXTURE)
		{
			d.texture_diffuse_index = slice_of(aiTextureType_DIFFUSE);
		}
		d.texture_specular_index = slice_of(aiTextureType_SPECULAR);
		d.texture_normals_index = slice_of(aiTextureType_NORMALS);
		if (d.texture_normals_index == GDR_NO_TEXTURE)
		{
			// OBJ "bump" / "map_bump" textures are height maps
			d.texture_normals_index = slice_of(aiTextureType_HEIGHT);
			if (d.texture_normals_index != GDR_NO_TEXTURE)
			{
				d.flags |= GDR_MATERIAL_BUMP_MAP;
			}
		}
		d.texture_opacity_index = slice_of(aiTextureType_OPACITY);
		if (d.texture_opacity_index != GDR_NO_TEXTURE || textures.has_cutout_alpha(d.texture_diffuse_index))
		{
			// Cut outs are foliage and cards, seen from both sides
			d.flags |= GDR_MATERIAL_ALPHA_TEST | GDR_MATERIAL_DOUBLE_SIDED;
		}
		int two_sided = 0;
		if (material.Get(AI_MATKEY_TWOSIDED, two_sided) == aiReturn_SUCCESS && two_sided != 0)
		{
			d.flags |= GDR_MATERIAL_DOUBLE_SIDED;
		}
		return d;
	}
}
