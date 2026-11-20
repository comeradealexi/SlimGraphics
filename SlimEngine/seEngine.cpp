#include "se_engine_pch.h"
#include "seEngine.h"

#include <cstdio>
#include <cstdlib>

extern "C"
{
	__declspec(dllimport) void OutputDebugStringA(const char* lpOutputString);
}

namespace se
{
	namespace Dbg
	{
#ifdef SE_USE_DEBUG_OUTPUT
		// Debug output goes to the debugger. Set the SE_LOG_FILE environment variable to also append it to a file
		// (useful when running without a debugger attached, e.g. automated runs).
		static FILE* GetLogFile()
		{
			static FILE* log_file = []() -> FILE*
				{
					char* path = nullptr;
					size_t length = 0;
					if (_dupenv_s(&path, &length, "SE_LOG_FILE") != 0 || path == nullptr)
					{
						return nullptr;
					}
					FILE* f = nullptr;
					fopen_s(&f, path, "a");
					free(path);
					return f;
				}();
			return log_file;
		}

		void Print(const char* fmt, ...)
		{
			char bfr[1024 * 8];

			va_list args;
			va_start(args, fmt);
			vsprintf_s(bfr, fmt, args);
			va_end(args);

			OutputDebugStringA(bfr);
			OutputDebugStringA("\n");
			if (FILE* log_file = GetLogFile())
			{
				fprintf(log_file, "%s\n", bfr);
				fflush(log_file);
			}
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
				Dbg::Print("%s", bfr);
			}
			Dbg::Print("%s(%s): ", file, line);
			Dbg::Print("Function: %s", function);
		}
#endif
	}
}
