#pragma once

#include <string>
#include <vector>

namespace cf
{

class Object;

// Saves / loads the whole scene (objects + their components) to JSON files
// under assets/scenes/.
struct Scene
{
	static void save(const std::string &file_path);
	static bool load(const std::string &file_path);   // clears the scene first
	static bool remove(const std::string &file_path); // delete the scene file

	static std::vector<std::string> list(); // scene names (no ext)
	static std::string file_path(const std::string &name);
};

// A reusable object template: one Object plus its component values and its whole
// child subtree, serialized to a JSON file under assets/prefabs/. Save any object
// as a prefab, then instantiate copies of it into the scene from the Prefabs panel.
struct Prefab
{
	// Serialize `root` (and its descendants) into assets/prefabs/<name>.json.
	static bool save(Object *root, const std::string &name);
	// Spawn a copy of the prefab file into the current scene; returns the new root
	// (already selected), or nullptr on failure.
	static Object *instantiate(const std::string &file_path);

	static bool remove(const std::string &file_path); // delete the prefab file
	static std::vector<std::string> list();           // prefab names (no ext)
	static std::string file_path(const std::string &name);
};

} // namespace cf
