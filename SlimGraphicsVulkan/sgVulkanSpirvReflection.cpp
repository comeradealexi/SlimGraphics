#include "pch.h"
#include "sgVulkanSpirvReflection.h"
#include <unordered_map>
#include <cstring>

namespace sg
{
	namespace Vulkan
	{
		namespace
		{
			// SPIR-V opcodes / enums used below (see the SPIR-V specification)
			enum Op : u32
			{
				OpName = 5,
				OpEntryPoint = 15,
				OpTypeImage = 25,
				OpTypeSampler = 26,
				OpTypeSampledImage = 27,
				OpTypeArray = 28,
				OpTypeRuntimeArray = 29,
				OpTypeStruct = 30,
				OpTypePointer = 32,
				OpConstant = 43,
				OpVariable = 59,
				OpDecorate = 71,
			};

			enum Decoration : u32
			{
				DecorationBlock = 2,
				DecorationBufferBlock = 3,
				DecorationLocation = 30,
				DecorationBinding = 33,
				DecorationDescriptorSet = 34,
			};

			enum StorageClass : u32
			{
				StorageClassUniformConstant = 0,
				StorageClassInput = 1,
				StorageClassUniform = 2,
				StorageClassStorageBuffer = 12,
			};

			constexpr u32 kDimBuffer = 5;
			constexpr u32 kSpirvMagic = 0x07230203;

			struct TypeInfo
			{
				u32 opcode = 0;
				u32 operand0 = 0; // Pointer: storage class | Array: element type | Image: dim
				u32 operand1 = 0; // Pointer: pointee type | Array: length constant id | Image: 'sampled' (1 = sampled, 2 = storage)
			};

			struct VariableInfo
			{
				u32 type_id = 0;
				u32 storage_class = 0;
			};

			struct DecorationInfo
			{
				bool has_set = false;
				bool has_binding = false;
				bool has_location = false;
				bool block = false;
				bool buffer_block = false;
				u32 set = 0;
				u32 binding = 0;
				u32 location = 0;
			};

			std::string read_string(const u32* words, u32 word_count)
			{
				const char* chars = reinterpret_cast<const char*>(words);
				const size_t max_len = static_cast<size_t>(word_count) * 4;
				return std::string(chars, strnlen(chars, max_len));
			}

			bool to_stage(u32 execution_model, vk::ShaderStageFlagBits& stage)
			{
				switch (execution_model)
				{
				case 0: stage = vk::ShaderStageFlagBits::eVertex; return true;
				case 1: stage = vk::ShaderStageFlagBits::eTessellationControl; return true;
				case 2: stage = vk::ShaderStageFlagBits::eTessellationEvaluation; return true;
				case 3: stage = vk::ShaderStageFlagBits::eGeometry; return true;
				case 4: stage = vk::ShaderStageFlagBits::eFragment; return true;
				case 5: stage = vk::ShaderStageFlagBits::eCompute; return true;
				case 5364: stage = vk::ShaderStageFlagBits::eTaskEXT; return true;
				case 5365: stage = vk::ShaderStageFlagBits::eMeshEXT; return true;
				default: return false;
				}
			}
		}

		bool reflect_spirv(const u32* code, size_t word_count, SpirvReflection& out)
		{
			out = {};
			if (word_count < 5 || code[0] != kSpirvMagic)
			{
				return false;
			}

			std::unordered_map<u32, TypeInfo> types;
			std::unordered_map<u32, VariableInfo> variables;
			std::unordered_map<u32, DecorationInfo> decorations;
			std::unordered_map<u32, std::string> names;
			std::unordered_map<u32, u32> constants;
			bool found_entry_point = false;

			size_t offset = 5;
			while (offset < word_count)
			{
				const u32 instruction = code[offset];
				const u32 length = instruction >> 16;
				const u32 opcode = instruction & 0xffff;
				if (length == 0 || offset + length > word_count)
				{
					return false; // Malformed
				}
				const u32* ops = code + offset + 1;
				const u32 op_count = length - 1;

				switch (opcode)
				{
				case OpEntryPoint:
					// DXC emits a single entry point per module
					if (!found_entry_point && op_count >= 3 && to_stage(ops[0], out.stage))
					{
						out.entry_point = read_string(ops + 2, op_count - 2);
						found_entry_point = true;
					}
					break;
				case OpName:
					if (op_count >= 2)
					{
						names[ops[0]] = read_string(ops + 1, op_count - 1);
					}
					break;
				case OpDecorate:
					if (op_count >= 2)
					{
						DecorationInfo& d = decorations[ops[0]];
						switch (ops[1])
						{
						case DecorationBlock: d.block = true; break;
						case DecorationBufferBlock: d.buffer_block = true; break;
						case DecorationLocation: if (op_count >= 3) { d.has_location = true; d.location = ops[2]; } break;
						case DecorationBinding: if (op_count >= 3) { d.has_binding = true; d.binding = ops[2]; } break;
						case DecorationDescriptorSet: if (op_count >= 3) { d.has_set = true; d.set = ops[2]; } break;
						default: break;
						}
					}
					break;
				case OpTypeImage:
					if (op_count >= 7) types[ops[0]] = { opcode, ops[2], ops[6] };
					break;
				case OpTypeSampler:
				case OpTypeStruct:
					if (op_count >= 1) types[ops[0]] = { opcode, 0, 0 };
					break;
				case OpTypeSampledImage:
				case OpTypeRuntimeArray:
					if (op_count >= 2) types[ops[0]] = { opcode, ops[1], 0 };
					break;
				case OpTypeArray:
				case OpTypePointer:
					if (op_count >= 3) types[ops[0]] = { opcode, ops[1], ops[2] };
					break;
				case OpConstant:
					if (op_count >= 3) constants[ops[1]] = ops[2];
					break;
				case OpVariable:
					if (op_count >= 3) variables[ops[1]] = { ops[0], ops[2] };
					break;
				default:
					break;
				}
				offset += length;
			}

			if (!found_entry_point)
			{
				return false;
			}

			for (const auto& [id, variable] : variables)
			{
				auto decoration_it = decorations.find(id);
				const DecorationInfo* decoration = decoration_it != decorations.end() ? &decoration_it->second : nullptr;

				if (variable.storage_class == StorageClassInput)
				{
					// Vertex inputs (built-ins such as SV_VertexID have no Location decoration)
					if (out.stage == vk::ShaderStageFlagBits::eVertex && decoration && decoration->has_location)
					{
						SpirvReflection::VertexInput input;
						input.location = decoration->location;
						auto name_it = names.find(id);
						if (name_it != names.end())
						{
							const std::string prefix = "in.var.";
							input.semantic = name_it->second.compare(0, prefix.size(), prefix) == 0 ? name_it->second.substr(prefix.size()) : name_it->second;
						}
						out.vertex_inputs.push_back(input);
					}
					continue;
				}

				if (!decoration || !decoration->has_binding)
				{
					continue;
				}

				auto pointer_it = types.find(variable.type_id);
				if (pointer_it == types.end() || pointer_it->second.opcode != OpTypePointer)
				{
					continue;
				}

				// Unwrap arrays of resources
				u32 type_id = pointer_it->second.operand1;
				u32 count = 1;
				for (auto type_it = types.find(type_id); type_it != types.end(); type_it = types.find(type_id))
				{
					if (type_it->second.opcode == OpTypeArray)
					{
						auto constant_it = constants.find(type_it->second.operand1);
						count *= constant_it != constants.end() ? constant_it->second : 1;
						type_id = type_it->second.operand0;
					}
					else if (type_it->second.opcode == OpTypeRuntimeArray)
					{
						count = 0; // Unbounded
						type_id = type_it->second.operand0;
					}
					else
					{
						break;
					}
				}

				auto type_it = types.find(type_id);
				if (type_it == types.end())
				{
					continue;
				}

				SpirvReflection::DescriptorBinding binding;
				binding.set = decoration->has_set ? decoration->set : 0;
				binding.binding = decoration->binding;
				binding.count = count;

				const TypeInfo& type = type_it->second;
				if (variable.storage_class == StorageClassStorageBuffer)
				{
					binding.type = vk::DescriptorType::eStorageBuffer;
				}
				else if (variable.storage_class == StorageClassUniform)
				{
					auto struct_decoration = decorations.find(type_id);
					const bool buffer_block = struct_decoration != decorations.end() && struct_decoration->second.buffer_block;
					binding.type = buffer_block ? vk::DescriptorType::eStorageBuffer : vk::DescriptorType::eUniformBuffer;
				}
				else if (type.opcode == OpTypeSampler)
				{
					binding.type = vk::DescriptorType::eSampler;
				}
				else if (type.opcode == OpTypeSampledImage)
				{
					binding.type = vk::DescriptorType::eCombinedImageSampler;
				}
				else if (type.opcode == OpTypeImage)
				{
					const bool storage = type.operand1 == 2;
					if (type.operand0 == kDimBuffer)
					{
						binding.type = storage ? vk::DescriptorType::eStorageTexelBuffer : vk::DescriptorType::eUniformTexelBuffer;
					}
					else
					{
						binding.type = storage ? vk::DescriptorType::eStorageImage : vk::DescriptorType::eSampledImage;
					}
				}
				else
				{
					continue; // Not a descriptor type we know about
				}
				out.bindings.push_back(binding);
			}

			std::sort(out.bindings.begin(), out.bindings.end(), [](const auto& a, const auto& b) { return a.set != b.set ? a.set < b.set : a.binding < b.binding; });
			std::sort(out.vertex_inputs.begin(), out.vertex_inputs.end(), [](const auto& a, const auto& b) { return a.location < b.location; });
			return true;
		}
	}
}
