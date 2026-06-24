#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

#include <random>
#include <vector>

namespace cf
{

// One GPU billboard instance (matches the particle.vert attribute layout).
struct ParticleInstance
{
	glm::vec3 pos;
	float size;
	glm::vec4 color; // rgb may exceed 1 for additive HDR glow
	glm::vec3 vel;   // for velocity-stretched billboards (rain streaks)
};

// A CPU-simulated billboard particle emitter. Spawns sprites from the owning
// Object's Transform, integrates them under gravity/drag, and fades size+colour
// over their life. Registers with the RendererManager, which draws every emitter
// after the transparent pass. Pairs with water for fountains + splashes.
class ParticleComponent : public Component
{
public:
	enum class Shape
	{
		Point,
		Disk,
		Sphere,
		Cone,
		Ring
	};
	enum class Blend
	{
		Alpha,
		Additive
	};
	enum class Mode
	{
		Emitter,
		Bolt
	}; // Emitter = normal sim; Bolt = jagged lightning

	// Emission.
	Mode mode = Mode::Emitter;
	bool enabled = true;
	float rate = 80.0f; // continuous particles/second (0 = burst-only)
	float lifetime = 1.6f;
	float lifetime_var = 0.3f;
	float start_speed = 3.2f;
	float speed_var = 0.7f;
	float spread = 22.0f;                    // cone half-angle (degrees) around emitDir
	glm::vec3 emit_dir = {0.0f, 1.0f, 0.0f}; // local; rotated by the Transform
	Shape shape = Shape::Cone;
	float emit_radius = 0.25f; // disk/sphere spawn radius

	// Motion.
	float gravity = -6.0f; // added to velocity.y each second
	float drag = 0.2f;
	float swirl = 0.0f;      // tangential accel around emitDir (orbit -> portals)
	float attraction = 0.0f; // radial accel toward the emitDir axis (tightens swirl)
	float stretch = 0.0f;    // elongate billboards along velocity (0 = round; rain streaks)

	// Ground splashes: when a particle falls below `groundY`, kill it and spawn a few
	// tiny upward droplets there (rain hitting the floor).
	bool splash_on_ground = false;
	float ground_y = 0.0f;

	// Bolt mode (lightning): a jagged line of bright billboards from the emitter along
	// emitDir, re-randomised on a flicker timer.
	float bolt_length = 4.0f;
	int bolt_segments = 14;
	float bolt_jitter = 0.5f; // sideways kink amplitude
	float flicker_hz = 14.0f; // re-strikes per second

	// Appearance.
	float start_size = 0.22f;
	float end_size = 0.04f;
	glm::vec4 start_color = {0.75f, 0.88f, 1.0f, 0.9f};
	glm::vec4 end_color = {0.45f, 0.65f, 1.0f, 0.0f};
	Blend blend = Blend::Alpha;
	bool soft = true;
	int max_particles = 2500;

	void on_attach() override;
	void update(float dt) override;
	~ParticleComponent() override;

	// Emit n particles immediately (e.g. a water splash). If atWorld is given, they
	// spawn there instead of at the emitter; extraVel is added to each.
	void burst(int n, const glm::vec3 *at_world = nullptr,
	           const glm::vec3 &extra_vel = glm::vec3(0.0f));

	// Apply sensible defaults for a named look (also sets blend/shape/colours).
	enum class Preset
	{
		Fountain,
		Splash,
		Fire,
		Smoke,
		Sparks,
		Portal,
		Lightning,
		Rain,
		Snow,
		Embers,
		Magic,
		Explosion,
		Mist
	};
	void apply_preset(Preset p);

	const std::vector<ParticleInstance> &instances() const { return m_instances; }

private:
	struct Particle
	{
		glm::vec3 pos;
		glm::vec3 vel;
		float age;
		float life;
		bool splash = false; // a ground-splash droplet (won't re-splash)
	};

	void spawn_one(const glm::vec3 *at_world, const glm::vec3 &extra_vel);
	void build_bolt();           // lay out the lightning billboards
	float rnd();                 // 0..1
	float rnd(float a, float b); // a..b

	std::vector<Particle> m_particles;
	std::vector<ParticleInstance> m_instances;
	float m_accum = 0.0f;      // fractional emission carry
	float m_bolt_timer = 0.0f; // flicker timer for Bolt mode
	std::mt19937 m_rng{0x9e3779b9u};
};

} // namespace cf
