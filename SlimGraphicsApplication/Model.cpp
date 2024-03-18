#include "Model.h"

//assimp
#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>
#include <limits>
#include <vector>

using namespace sg;

Model::Model(Device* device, const std::string& file)
{
	Assimp::Importer ai_importer;
	int flags = aiProcess_Triangulate
		| aiProcess_PreTransformVertices
		| aiProcess_CalcTangentSpace
		| aiProcess_GenSmoothNormals
		| aiProcess_JoinIdenticalVertices;

	const aiScene* aScene = ai_importer.ReadFile(file.c_str(), flags); //By passing file path, allows assimp to auto load material files.
}
