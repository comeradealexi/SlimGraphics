#pragma once
#include <stdint.h>

#ifdef _DEBUG
#define SE_USE_DEBUG_OUTPUT
#define SE_USE_DEBUG_ASSERTS
#endif

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)

#ifdef SE_USE_DEBUG_OUTPUT
namespace se
{
	namespace Dbg
	{
		void Print(const char* fmt, ...);
	}
}
#define seWriteLine(fmt, ...) { se::Dbg::Print(fmt, ## __VA_ARGS__);}
#else
#define seWriteLine(...)
#endif

#ifdef SE_USE_DEBUG_ASSERTS
namespace se
{
	namespace Dbg
	{
		void Assert(const char* file, const char* line, const char* function, const char* asrt, const char* fmt, ...);
	}
}
#define seAssert(expression, fmt, ...) { if (expression == false) { se::Dbg::Assert(__FILE__, LINE_STRING, __FUNCTION__, STRINGIZE(expression) , fmt, ## __VA_ARGS__); 		__debugbreak(); } }
#else
#define seAssert(...)
#endif