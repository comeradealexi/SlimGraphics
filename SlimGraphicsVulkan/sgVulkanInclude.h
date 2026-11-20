#pragma once
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <Windows.h>
#include <seEngine.h>

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>

// Vulkan-Hpp reports errors by throwing. The engine builds without C++ exceptions, so such errors terminate, which matches the
// D3D12 backend's CHECKHR policy. Calls whose non-success results are expected (swap chain acquire/present, query results)
// use the C entry points through the RAII dispatcher instead.
#define CHECKVK(x) { const VkResult ___vr = static_cast<VkResult>(x); seAssert(___vr == VK_SUCCESS, "VkResult failure: %i", static_cast<int>(___vr)); }
