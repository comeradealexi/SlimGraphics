#include "gdrJSON.h"
#include <seEngineBasicFileIO.h>
#include <fstream>
#include <seEngine.h>

namespace gdr
{
	void serialise(const SceneNode& node, const char* file_path)
	{
		nlohmann::json j = node;
		std::ofstream os(file_path, std::ofstream::ate);
		seAssert(os.good(), "Failed To Write File %s", file_path);
		if (os.good())
		{
			os << j;
			os.close();
		}
	}

	void deserialise(SceneNode& node, const char* file_path)
	{
		std::vector<uint8_t> file_data = se::BasicFileIO::load_file(file_path);
		node = nlohmann::json::parse(file_data);
	}

	size_t SceneNode::generate_unique_id()
	{
		static std::atomic_size_t unique_id;
		return unique_id++;
	}

}