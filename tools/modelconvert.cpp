// modelconvert -- convert any Assimp-supported model file to binary glTF (.glb).
//
//   modelconvert <input> [output.glb]
//
// Supports OBJ/FBX/3DS/DAE/STL/PLY/glTF/.blend (and more). Materials, colors,
// and textures are carried into the .glb (binary glTF embeds textures), which
// CorForge loads via "Add object -> Model".
//
// Texture resolution: many models (e.g. OBJ packs) reference textures by bare
// filename while the images live in a separate folder. So after import we scan
// the model's folder tree for image files and rewrite each material's texture
// path to the real file, so Assimp can embed it.

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

static std::string lower(std::string s)
{
	for (char &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::printf("usage: modelconvert <input-model> [output.glb]\n");
		return 1;
	}
	const std::string input = argv[1];
	std::string output;
	if (argc >= 3)
	{
		output = argv[2];
	}
	else
	{
		const size_t dot = input.find_last_of('.');
		output = (dot == std::string::npos ? input : input.substr(0, dot)) + ".glb";
	}

	Assimp::Importer importer;
	const aiScene *scene =
	    importer.ReadFile(input, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
	                                 aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace |
	                                 aiProcess_ImproveCacheLocality);
	if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
	{
		std::fprintf(stderr, "import failed (%s): %s\n", input.c_str(), importer.GetErrorString());
		return 2;
	}
	std::printf("imported %s: %u meshes, %u materials\n", input.c_str(), scene->mNumMeshes,
	            scene->mNumMaterials);

	// Index every image file under the model's folder (recursively): name -> path.
	std::unordered_map<std::string, std::string> images;
	std::error_code ec;
	const fs::path dir = fs::path(input).parent_path();
	for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec))
	{
		if (ec || !it->is_regular_file())
			continue;
		const std::string ext = lower(it->path().extension().string());
		if (ext == ".png" || ext == ".tga" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp")
			images[lower(it->path().filename().string())] = it->path().string();
	}
	std::printf("found %zu image file(s) under %s\n", images.size(), dir.string().c_str());

	// Resolve each material's diffuse/base-color texture to a real file path.
	const aiTextureType types[] = {aiTextureType_DIFFUSE, aiTextureType_BASE_COLOR};
	std::unordered_set<std::string> ok, missing;
	for (unsigned i = 0; i < scene->mNumMaterials; ++i)
	{
		aiMaterial *mat = scene->mMaterials[i];
		for (aiTextureType t : types)
		{
			aiString tp;
			if (mat->GetTexture(t, 0, &tp) != AI_SUCCESS)
				continue;
			const std::string base = lower(fs::path(tp.C_Str()).filename().string());
			const auto f = images.find(base);
			if (f != images.end())
			{
				aiString np(f->second);
				mat->AddProperty(&np, AI_MATKEY_TEXTURE(t, 0)); // rewrite to real path
				ok.insert(base);
			}
			else
			{
				missing.insert(base);
			}
		}
	}
	std::printf("textures resolved: %zu, missing: %zu\n", ok.size(), missing.size());
	for (const std::string &m : missing)
		std::printf("  MISSING: %s\n", m.c_str());

	Assimp::Exporter exporter;
	if (exporter.Export(scene, "glb2", output) != aiReturn_SUCCESS)
	{
		std::fprintf(stderr, "export failed (%s): %s\n", output.c_str(), exporter.GetErrorString());
		return 3;
	}
	std::printf("wrote %s\n", output.c_str());
	return 0;
}
