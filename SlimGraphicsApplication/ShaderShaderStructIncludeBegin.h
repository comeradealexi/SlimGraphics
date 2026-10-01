#ifndef HEADER_SHARED_STRUCTURE_BEGIN
#define HEADER_SHARED_STRUCTURE_BEGIN
	#ifdef __cplusplus
	#include <stdint.h>
	#define row_major 
	#define bool uint32_t
	namespace ShaderStructs
	{
		using float4 = DirectX::XMFLOAT4A;
		using float4x4 = DirectX::XMMATRIX;
		using uint = uint32_t;
		using int4 = DirectX::XMINT4;
	#else

	#endif
#endif