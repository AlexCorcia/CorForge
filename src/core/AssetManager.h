#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cf
{

class Mesh;
class Texture;

// Singleton catalog of meshes + textures. Combines built-in primitives /
// procedural textures with files discovered under the assets/ folders:
//   assets/meshes/*.obj        -> selectable meshes
//   assets/textures/*.png|jpg  -> selectable albedo maps
// Resources are loaded lazily and cached by name.
class AssetManager
{
public:
	static AssetManager &instance();

	AssetManager(const AssetManager &) = delete;
	AssetManager &operator=(const AssetManager &) = delete;

	// Scan the asset folders (call after the GL context exists).
	void init();

	const std::vector<std::string> &mesh_names() const { return m_mesh_names; }
	const std::vector<std::string> &texture_names() const { return m_texture_names; }
	const std::vector<std::string> &model_names() const { return m_model_names; }

	std::shared_ptr<Mesh> mesh(const std::string &name);
	std::shared_ptr<Texture> texture(const std::string &name);
	std::shared_ptr<Texture> texture_linear(const std::string &name); // normal/data maps
	std::string model_path(const std::string &name) const;            // glTF file path

private:
	AssetManager() = default;

	void scan_meshes();
	void scan_textures();
	void scan_models();
	void seed_sample_textures(); // write a sample image if the folder has none
	void seed_sample_models();   // write a sample box.gltf if the folder has none

	std::vector<std::string> m_mesh_names;
	std::vector<std::string> m_texture_names;
	std::vector<std::string> m_model_names;

	// name -> file path (only for file-backed assets)
	std::unordered_map<std::string, std::string> m_mesh_files;
	std::unordered_map<std::string, std::string> m_texture_files;
	std::unordered_map<std::string, std::string> m_model_files;

	std::unordered_map<std::string, std::shared_ptr<Mesh>> m_mesh_cache;
	std::unordered_map<std::string, std::shared_ptr<Texture>> m_texture_cache;
};

} // namespace cf
