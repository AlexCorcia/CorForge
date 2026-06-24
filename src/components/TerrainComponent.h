#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <random>
#include <vector>

namespace cf
{

class TerrainPropComponent;

// Generates a procedural heightmap terrain mesh (fractal-noise hills/valleys) and
// draws it with the terrain shader, which colours it by height + slope
// (sand/grass/rock/snow). Rebuild after editing any parameter.
class TerrainComponent : public Component
{
public:
	float size = 50.0f;        // world extent (size x size, centred on the object)
	int resolution = 96;       // grid divisions per side
	float height_scale = 9.0f; // max terrain height
	float frequency = 0.05f;   // base noise frequency
	int octaves = 6;           // fractal detail layers
	float lacunarity = 2.0f;   // frequency step per octave
	float gain = 0.5f;         // amplitude step per octave
	std::uint32_t seed = 1337;
	bool island = true;         // fade the edges down (a floating landmass)
	float edge_falloff = 0.35f; // how far in the island falloff starts (0..1)
	bool donut = false;         // ring of land with a central basin (an atoll)
	float ring_radius = 0.55f;  // crest position, fraction of the half-extent
	float ring_width = 0.32f;   // half-thickness of the raised ring
	glm::vec3 grass_color = {0.30f, 0.55f, 0.22f};
	bool low_poly = true;    // flat-shaded facets (the diorama look)
	bool slab = true;        // cut a solid block with cliff sides + a base
	float base_depth = 7.0f; // how far the slab extends below the lowest point

	// Vegetation / props scattered over the surface by height + slope.
	bool scatter = true;
	int tree_count = 90; // target number of trees actually placed
	int rock_count = 30;
	float prop_scale = 1.0f;      // overall size multiplier
	float water_level = 0.0f;     // props are only placed above this world Y
	float tree_min_height = 1.2f; // trees stay this far above the water (low ground = bare)
	float forest_scale = 14.0f;   // size of forest clumps (trees cluster, leaving clearings)
	glm::vec3 tree_color = {0.18f, 0.42f, 0.18f};
	bool collider = true; // give the surface a heightfield collider (physics)

	void on_attach() override;
	void update(float dt) override;
	~TerrainComponent() override;
	void regenerate(); // rebuild the mesh (and re-scatter props) for the current params

	// Sample the surface at a WORLD (x,z): outputs world ground height + slope normal.
	// Returns false if (x,z) is outside the terrain footprint. Used by the physics
	// step for the heightfield collider. Assumes the terrain is unrotated/unscaled.
	bool sample_height(float world_x, float world_z, float &out_y, glm::vec3 &out_normal) const;

private:
	void build_mesh();
	void ensure_collider();
	void scatter_props();
	void clear_props();
	float rnd(float a, float b);

	// Heightfield cached from the last buildMesh so props can be scattered lazily
	// (on the first update, after serialized params + transform are in place).
	std::vector<float> m_h;
	int m_n = 0;
	float m_step = 1.0f, m_min_h = 0.0f, m_range = 1.0f;
	bool m_scatter_dirty = false;

	std::vector<TerrainPropComponent *> m_props;
	std::mt19937 m_rng{0x7E44A1FDu};
};

} // namespace cf
