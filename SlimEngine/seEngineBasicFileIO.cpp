#include "se_engine_pch.h"
#include "seEngineBasicFileIO.h"

#include <fstream>

namespace se
{
	std::vector<uint8_t> BasicFileIO::LoadFile(const char* file_path)
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
}
