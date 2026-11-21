#pragma once
#include <sgPlatformInclude.h>
#include "sgUploadHeap.h"
#include <DirectXMath.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "gdrShaderStructures.h"

struct aiScene;
struct aiMaterial;

namespace gdr
{
	// Every material texture of the GPU driven scene, resampled to one size and stored as the slices of a single
	// Texture2DArray. Materials refer to slices by index, so all models render in one mesh dispatch with one texture binding,
	// and shaders never index resources non-uniformly.
	class MaterialTextures
	{
	public:
		static constexpr sg::u32 kSliceSize = 1024;		// Width and height of every slice (mip 0)
		static constexpr sg::u32 kUploadBudget = 96u << 20; // Bytes of slice data uploaded per frame (the upload heap is per frame)

		// Each returns the texture's slice (loading it once per key), or GDR_NO_TEXTURE when it cannot be decoded
		sg::u32 add_file(const std::string& path);
		sg::u32 add_encoded(const std::string& key, const uint8_t* data, size_t size); // A compressed image in memory
		sg::u32 add_rgba8(const std::string& key, const uint8_t* rgba, sg::u32 width, sg::u32 height);

		// Whether the slice has cut out (alpha < 0.5) texels, so materials using it as diffuse need alpha testing
		bool has_cutout_alpha(sg::u32 slice) const { return slice < slices.size() && slices[slice].cutout_alpha; }

		// Creates the texture array (once all models loaded) and uploads slices, within kUploadBudget per call. Call every frame
		// until is_uploaded(): the array always has at least one (white) slice, so it can be bound before anything loads.
		void upload(sg::SharedPtr<sg::Device>& device, sg::Ptr<sg::UploadHeap>& upload_heap);
		bool is_uploaded() const { return texture && next_upload == slices.size(); }
		sg::ShaderResourceView& get_srv() { return srv; } // Non const: sg::Binding::set_srv takes a reference
		sg::u32 get_slice_count() const { return static_cast<sg::u32>(slices.size()); }

	private:
		struct Slice
		{
			std::vector<uint8_t> pixels; // kSliceSize x kSliceSize RGBA8 (mips are built at upload)
			bool cutout_alpha = false;
		};
		std::vector<Slice> slices;
		std::unordered_map<std::string, sg::u32> slice_lookup;
		size_t next_upload = 0;
		sg::SharedPtr<sg::Texture> texture;
		sg::ShaderResourceView srv;
	};

	// Material without textures: white, slightly glossy
	ShaderStructs::MaterialData default_material();

	// Converts an assimp material (colours, Blinn-Phong exponent and diffuse / base colour, specular, normal or bump, and opacity
	// textures, external or embedded) to the GPU material, adding its textures to 'textures'
	ShaderStructs::MaterialData load_material(const aiScene& scene, const aiMaterial& material, const std::string& model_path, MaterialTextures& textures);

	// The render bucket (GDR_BUCKET_*) of meshlets using a material: each bucket has its own pipeline state and shaders
	inline sg::u32 bucket_of_material(const ShaderStructs::MaterialData& material)
	{
		if ((material.flags & GDR_MATERIAL_TERRAIN) != 0u)
		{
			return GDR_BUCKET_TERRAIN;
		}
		if ((material.flags & (GDR_MATERIAL_ALPHA_TEST | GDR_MATERIAL_DOUBLE_SIDED)) != 0u)
		{
			return GDR_BUCKET_FOLIAGE;
		}
		return GDR_BUCKET_OPAQUE;
	}
}
