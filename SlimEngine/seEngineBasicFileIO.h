#pragma once
#include <vector>

namespace se
{
	class BasicFileIO
	{
	public:
		// Returns empty vector on failure
		static std::vector<uint8_t> LoadFile(const char* name);
	};
}
