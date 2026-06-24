#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

#include <unordered_map>

namespace cf
{

class ParticleComponent;
class Object;

// Turns its Object into an animated water surface: a Plane drawn with the water
// shader (moving wave normals + planar reflection + Fresnel transparency). On
// attach it sets up the Plane mesh, the water material and the planar-reflection
// flags, so it reflects the scene above and you can see the bottom through it.
class WaterComponent : public Component
{
public:
	glm::vec3 color = {0.08f, 0.40f, 0.85f}; // base blue
	float opacity = 0.9f;                    // fairly opaque (flat toon look)
	bool round = false;                      // circular disk instead of a square
	float calm = 0.0f;                       // 0 = ocean waves, 1 = flat calm puddle

	// Splash: the water watches every rigidbody and, when one breaks its surface
	// moving down, spawns a burst of droplets at the impact point (via an auto-added
	// ParticleComponent on this object). This is the water detecting the collision,
	// so any falling body splashes -- no per-object emitter needed.
	bool splash = true;
	float splash_threshold = 1.2f; // minimum downward speed to trigger a splash
	float splash_amount = 1.0f;    // scales the droplet count

	void on_attach() override;
	void update(float dt) override;
	void apply();        // push color/opacity to the sibling material (after edits/load)
	void rebuild_mesh(); // (re)build the surface mesh for the current `round` flag

	// Surface wave height at a world XZ (matches water.vert), for buoyancy. Add the
	// water object's Y to get the world surface level.
	static float wave_height(const glm::vec2 &world_xz, float time);

private:
	ParticleComponent *m_splash_fx = nullptr;         // auto-added droplet emitter
	std::unordered_map<const Object *, bool> m_above; // per-body: was above the surface?
};

} // namespace cf
