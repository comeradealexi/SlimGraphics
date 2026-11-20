#pragma once
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <Windows.h>
#include <seEngine.h>
#include <utility>

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
// The engine builds without C++ exceptions, so Vulkan-Hpp runs in its no-exception mode: calls return vk::Result or
// vk::ResultValue<T> (marked [[nodiscard]], so ignored results are compiler warnings) and every result is checked with
// CHECKVK, which reports the failing call and code. Vulkan-Hpp's own assert on failure is replaced by that report.
// This header must be the only place Vulkan-Hpp is included from, so every translation unit sees the same configuration.
#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT(expression)
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>

namespace sg
{
	namespace Vulkan
	{
		// Reports a failed Vulkan call in every build configuration (debug output, engine log, message box) and terminates:
		// the renderer has no recovery path for device loss, memory exhaustion or invalid usage.
		[[noreturn]] void report_fatal_error(const char* call, vk::Result result, const char* file, int line);

		inline void check_result(vk::Result result, const char* call, const char* file, int line)
		{
			if (result != vk::Result::eSuccess)
			{
				report_fatal_error(call, result, file, line);
			}
		}

		inline void check_result(VkResult result, const char* call, const char* file, int line)
		{
			check_result(static_cast<vk::Result>(result), call, file, line);
		}

		template <typename T>
		T check_result(vk::ResultValue<T>&& result, const char* call, const char* file, int line)
		{
			check_result(result.result, call, file, line);
			return std::move(result.value);
		}
	}
}

// Checks a vk::Result / VkResult (Vulkan-Hpp, the C API, VMA), or a vk::ResultValue<T> and evaluates to its value.
// Anything but VK_SUCCESS is fatal; calls with other expected results (swap chain, queries) handle them explicitly.
#define CHECKVK(call) ::sg::Vulkan::check_result((call), #call, __FILE__, __LINE__)
