#pragma once

#include "gfx/Mesh.h"
#include "gfx/Texture.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace cf
{

// One drawable piece of a loaded model (a glTF primitive).
struct ModelPrimitive
{
	std::shared_ptr<Mesh> mesh;
	glm::vec3 offset{0.0f}; // piece centre in model space (split mode only)
	glm::vec3 albedo{0.8f};
	std::shared_ptr<Texture> albedo_map; // null if the material has no base texture
	std::shared_ptr<Texture> normal_map; // tangent-space normal map (linear), or null
	float metallic{0.0f};
	float roughness{0.5f};
	std::shared_ptr<Texture> metallic_roughness_map;
	float opacity{1.0f};
	bool transparent{false};
};

// Load a .gltf / .glb into engine meshes + materials. Node transforms are baked
// in and the whole model is centered + scaled to ~2 units. Returns empty on
// failure (reason printed to stderr).
std::vector<ModelPrimitive> load_gltf(const std::string &path);

// Load a Wavefront .obj directly, reading its .mtl (Kd colors, Ns -> roughness,
// map_Kd textures resolved from the model's folder) and splitting by material
// into submeshes. No conversion to glTF needed.
std::vector<ModelPrimitive> load_obj_model(const std::string &path);

// Pick the right loader by file extension (.gltf/.glb -> glTF, .obj -> OBJ,
// else Assimp: .fbx/.dae/.3ds/.stl/.ply/.blend).
std::vector<ModelPrimitive> load_any_model(const std::string &path);

// Like loadAnyModel, but each mesh part is re-centred on its own centroid and its
// `offset` (where the part sits in the model) is filled in -- for splitting a
// multi-part model into individual objects with sensible pivots.
std::vector<ModelPrimitive> load_model_parts(const std::string &path);

// Load a .obj as a single combined Mesh (all material groups merged, no
// materials, kept at its authored size). For OBJ files used as plain mesh assets.
// Throws std::runtime_error on failure.
std::shared_ptr<Mesh> load_obj(const std::string &path);

} // namespace cf
