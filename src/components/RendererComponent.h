#pragma once

#include "core/Component.h"
#include "gfx/Material.h"
#include "gfx/Mesh.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace cf
{

struct RenderContext;

// One mesh + its material (used for multi-material imported models).
struct Submesh
{
	std::shared_ptr<Mesh> mesh;
	Material material;
};

// Makes its Object drawable. Two modes:
//  - single mesh: `mesh` + a sibling MaterialComponent (primitives / editor)
//  - multi-material model: a list of `submeshes`, each with its own material
//    (imported glTF). When `submeshes` is non-empty it takes precedence.
class RendererComponent : public Component
{
public:
	std::shared_ptr<Mesh> mesh;
	std::string mesh_name = "Cube";

	std::vector<Submesh> submeshes; // non-empty for imported models
	std::string model_source;       // model asset name (for scene save/load)
	// When >= 0, this renderer holds a SINGLE part of a split multi-mesh model:
	// on load it re-imports loadModelParts(modelSource)[modelPart] instead of the
	// whole model. -1 = whole model (all submeshes) or a plain mesh.
	int model_part = -1;

	// Per-object reflection cubemap (allocated by RendererManager). 0 = none.
	unsigned int reflection_cube = 0;

	// Per-object planar reflection texture (for a flat mirror). 0 = none.
	unsigned int reflection_tex = 0;
	int reflection_tex_w = 0;
	int reflection_tex_h = 0;

	// Per-object planar reflection array (6 layers, for a box mirror). 0 = none.
	unsigned int reflection_array = 0;
	int reflection_array_w = 0;
	int reflection_array_h = 0;

	RendererComponent(); // mesh defaults to the engine default mesh
	void on_attach() override;
	~RendererComponent() override;

	// Draws only the submeshes/material matching the requested pass:
	// transparentPass=false => opaque geometry, true => alpha-blended.
	void draw(const RenderContext &ctx, bool transparent_pass) const;

	// Combined local-space bounds over the active geometry (for picking).
	bool local_bounds(glm::vec3 &mn, glm::vec3 &mx) const;

	// True if any of this object's geometry is alpha-blended (for sort/order).
	bool is_transparent() const;
};

} // namespace cf
