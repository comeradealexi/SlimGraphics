#pragma once
#include <vector>
#include <filesystem>

namespace se
{
	class BasicFileIO
	{
	public:
		// Returns empty vector on failure
		static std::vector<uint8_t> load_file(const char* name);
		
		static std::vector<std::string> find_files_recursive(const char* search_directory, const std::vector<const char*>& extensions = {});
	};
}
