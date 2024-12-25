#pragma once
#include "seEngine.h"
#include "sgPlatformForwardDeclare.h"

namespace sg
{
	template <typename T>
	static inline constexpr T AlignUp(T val, T align)
	{
		return (val + align - 1) / align * align;
	}
}