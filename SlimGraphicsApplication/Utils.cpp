#include "Utils.h"

std::string get_shader_path(std::string_view shader_name)
{
	std::string output = SHADER_COMPILER_FOLDER;
	output += "/";
	output += shader_name;
	output += ".";
	output += SHADER_COMPILER_EXTENSION;
	return output;
}