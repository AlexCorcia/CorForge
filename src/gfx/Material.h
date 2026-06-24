#pragma once

#include "gfx/Shader.h"
#include "gfx/Texture.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace cf
{

// Surface appearance for a RendererComponent. Holds the shader to draw with
// plus the parameters it reads. (Blinn-Phong for now; this is where PBR
// metallic/roughness will live in a later stage.)
struct Material
{
	std::shared_ptr<Shader> shader;

	glm::vec3 albedo{0.8f, 0.8f, 0.8f};
	float ambient = 0.08f; // flat ambient term (until full IBL)

	// PBR metallic-roughness.
	float metallic = 0.0f;
	float roughness = 0.5f;
	std::shared_ptr<Texture> metallic_roughness_map; // glTF: roughness=G, metallic=B (linear)

	// Strength of the sky's mirror (specular IBL) on this surface. 1 = full PBR
	// env reflection; 0 = matte (only diffuse sky lighting, no mirror). Lower it on
	// smooth/curved objects that look too reflective under a sky.
	float env_specular = 1.0f;

	// When `reflective` is on, the object mirrors the scene; `reflectivity` is
	// the blend strength. The method is chosen explicitly:
	//   - neither flag        -> cubemap (per-object, works on any shape)
	//   - reflectPlanar       -> exact planar reflection (best for a flat Plane)
	//   - reflectBox          -> per-face planar reflection (best for a Cube/box)
	// Planar/box only make sense on flat / boxy geometry; on other shapes they
	// look wrong (that is the user's choice).
	bool reflective = false;
	float reflectivity = 0.6f;
	bool reflect_planar = false;
	bool reflect_box = false;

	// Albedo texture (multiplied by `albedo`). If null, a 1x1 white texture is
	// used so the tint shows through unchanged.
	std::shared_ptr<Texture> albedo_map;
	std::string albedo_map_name = "None"; // which asset `albedoMap` came from (UI)
	glm::vec2 uv_scale{1.0f, 1.0f};       // tiling factor for the albedo map

	// Tangent-space normal map (surface detail). Null = use the geometric normal.
	std::shared_ptr<Texture> normal_map;
	std::string normal_map_name = "None";

	// Transparency. opacity < 1 (or `transparent`) => alpha-blended in a sorted
	// pass after the opaque geometry.
	float opacity = 1.0f;
	bool transparent = false;
	bool is_transparent() const { return transparent || opacity < 0.999f; }

	// Water only: 0 = full ocean waves + flowing bands, 1 = flat calm puddle.
	float calm = 0.0f;
};

} // namespace cf
