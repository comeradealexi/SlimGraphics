#include "gdrJSON.h"
#include <seEngineBasicFileIO.h>
#include <fstream>
#include <seEngine.h>

namespace gdr
{
	bool serialise(const SceneNode& node, const char* file_path)
	{
		nlohmann::json j = node;
		std::ofstream os(file_path, std::ofstream::ate);
		seAssert(os.good(), "Failed To Write File %s", file_path);
		if (os.good())
		{
			os << j;
			os.close();
			return true;
		}
		return false;
	}

	bool deserialise(SceneNode& node, const char* file_path)
	{
		std::vector<uint8_t> file_data = se::BasicFileIO::load_file(file_path);
		if (file_data.size() == 0)
			return false;
		node = nlohmann::json::parse(file_data);
		return true;
	}
}