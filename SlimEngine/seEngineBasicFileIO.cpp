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
			const std::streamoff uiFileSize = is.tellg();
			if (uiFileSize <= 0)
			{
				return m_data;
			}
			is.seekg(0);
			m_data.resize(uiFileSize);
			is.read((char*)m_data.data(), uiFileSize);
		}
		return m_data;
	}

	bool BasicFileIO::write_file(const char* data, size_t data_size, const char* file_path)
	{
		// Binary mode so data is written byte-for-byte (no CRLF translation); out|trunc replaces any existing file.
		std::ofstream os(file_path, std::ios::binary | std::ios::out | std::ios::trunc);
		if (os.good() == false)
		{
			return false;
		}
		os.write(data, data_size);
		os.close();
		return os.good();
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
						std::string file_extension = entry.path().extension().string();

						for (const char* extension : extensions)
						{
							if (strcmpi(file_extension.c_str(), extension) == 0)
							{
								return_paths.push_back(entry.path().string());
								break; // Matched; don't add the same file again for another extension
							}
						}
					}
				}
				else
				{
					return_paths.push_back(entry.path().string());
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
