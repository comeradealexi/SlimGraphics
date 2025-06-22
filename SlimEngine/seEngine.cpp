#pragma once
#include "se_engine_pch.h"
#include "seEngine.h"

extern "C"
{
	__declspec(dllimport) void OutputDebugStringA(const char* lpOutputString);
}

namespace se
{
	namespace Dbg
	{
#ifdef SE_USE_DEBUG_OUTPUT
		void Print(const char* fmt, ...)
		{
			char bfr[1024 * 8];

			va_list args;
			va_start(args, fmt);
			vsprintf_s(bfr, fmt, args);
			va_end(args);

			OutputDebugStringA(bfr);
			OutputDebugStringA("\n");
		}
#endif

#ifdef SE_USE_DEBUG_ASSERTS
			void Assert(const char* file, const char* line, const char* function, const char* asrt, const char* fmt, ...)
			{
				Dbg::Print("Assert: %s", asrt);
				{
					char bfr[1024 * 8];
					va_list args;
					va_start(args, fmt);
					vsprintf_s(bfr, fmt, args);
					va_end(args);
					OutputDebugStringA(bfr);
					OutputDebugStringA("\n");
				}
				Dbg::Print("%s(%s): ", file, line);
				Dbg::Print("Function: %s", function);
			}
#endif

	}
}