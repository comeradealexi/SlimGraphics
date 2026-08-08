#include "se_engine_pch.h"
#include "seEngineBasicFileIO.h"

#include <fstream>

namespace se
{
	std::vector<uint8_t> BasicFileIO::load_file(const char* file_path)
	{
		std::vector<uint8_t> m_data;
		{
			std::ifstream is(file_path, std::ios::binary | std::ios::ate);
			if (is.good() == false)
			{
				return m_data;
			}
			auto uiFileSize = is.tellg();
			is.seekg(0);
			m_data.resize(uiFileSize);
			is.read((char*)m_data.data(), uiFileSize);
		}
		return m_data;
	}

	void find_files_recursive_internal(std::filesystem::directory_iterator directory, std::vector<std::string>& return_paths, const std::vector<const char*>& extensions)
	{
		for (const auto& entry : std::filesystem::directory_iterator(directory))
		{
			if (entry.is_regular_file())
			{
				if (extensions.size())
				{
					if (entry.path().has_extension())
					{
						std::string file_extension = entry.path().extension().u8string();

						for (const char* extension : extensions)
						{
							if (strcmpi(file_extension.c_str(), extension) == 0)
							{
								return_paths.push_back(entry.path().u8string());
								continue;
							}
						}
					}
				}
				else
				{
					return_paths.push_back(entry.path().u8string());
				}
			}
			else if (entry.is_directory())
			{
				auto next_di = std::filesystem::directory_iterator(entry);
				find_files_recursive_internal(next_di, return_paths, extensions);
			}
		}
	}

	std::vector<std::string> BasicFileIO::find_files_recursive(const char* search_directory, const std::vector<const char*>& extensions)
	{
		std::vector<std::string> return_paths;

		auto di = std::filesystem::directory_iterator(search_directory);
		find_files_recursive_internal(di, return_paths, extensions);

		return return_paths;
	}
}
