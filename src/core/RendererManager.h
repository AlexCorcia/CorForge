#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cf
{

class CameraComponent;
class RendererComponent;
class LightComponent;
class SkyComponent;
class ParticleComponent;
class Shader;
class Mesh;

// A light flattened into the form the shader consumes.
struct GpuLight
{
	int type = 0; // 0 directional, 1 point, 2 spot
	glm::vec3 color{1.0f};
	float intensity = 1.0f;
	glm::vec3 position{0.0f};               // point / spot
	glm::vec3 direction{0.0f, -1.0f, 0.0f}; // travel direction (dir / spot)
	float range = 12.0f;
	float cos_inner = 0.95f;    // spot
	float cos_outer = 0.90f;    // spot
	int shadow2_d_index = -1;   // layer in the 2D shadow array (dir/spot)
	int shadow_cube_index = -1; // layer in the cube shadow array (point)
};

// Per-frame data the renderer feeds to each RendererComponent.
struct RenderContext
{
	glm::mat4 view{1.0f};
	glm::mat4 proj{1.0f};
	glm::vec3 camera_pos{0.0f};

	const GpuLight *lights = nullptr;
	int num_lights = 0;

	const glm::mat4 *light_space2_d = nullptr; // one per 2D shadow (dir/spot)
	int num_shadow2_d = 0;
	unsigned int shadow2_d_array = 0;         // GL_TEXTURE_2D_ARRAY depth
	unsigned int shadow_cube_array = 0;       // GL_TEXTURE_CUBE_MAP_ARRAY depth
	unsigned int shadow2_d_color_array = 0;   // transmittance colour (2D)
	unsigned int shadow_cube_color_array = 0; // transmittance colour (cube)
	float shadow_strength = 1.0f;

	// Environment proxy box (world-space scene AABB) for parallax-corrected
	// cubemap reflections: the reflection ray is intersected with this box so the
	// cubemap appears anchored to real geometry instead of infinitely far away.
	glm::vec3 refl_box_min{-1.0f};
	glm::vec3 refl_box_max{1.0f};

	// World-space clip plane (n.xyz, d) used during the planar-reflection pass to
	// discard geometry behind the mirror. Zero = no clipping.
	glm::vec4 clip_plane{0.0f};

	// Render target size in pixels (for planar mirrors sampling by screen UV).
	glm::vec2 screen_size{1.0f};

	// Sky / image-based lighting environment cubemap (linear HDR, mipmapped).
	bool has_sky = false;
	unsigned int env_cube = 0;
	float env_max_mip = 0.0f;
	float sky_intensity = 1.0f;

	// Seconds since start, for animated materials (e.g. water waves).
	float time = 0.0f;

	// Resolved scene depth texture + camera planes, for the water's shore foam
	// (compares the water surface depth to the geometry behind it). 0 = unavailable.
	unsigned int scene_depth = 0;
	float near_plane = 0.1f;
	float far_plane = 100.0f;

	// Output controls (toggled while capturing per-object reflections).
	bool reflections_enabled = true; // off while capturing a reflection cubemap
	bool apply_gamma = true;         // off while rendering into a linear capture

	// Forward-shader distance fog. The main view fogs in post-processing, but
	// reflection captures bypass post, so they apply fog here instead (on during
	// captures) -- otherwise the reflected world looks clear while the scene is hazy.
	bool apply_fog = false;
	glm::vec3 fog_color{0.5f, 0.55f, 0.65f};
	float fog_density = 0.0f;
};

// Singleton that tracks the cameras + drawables in the scene and renders a
// frame from the point of view of the main camera.
class RendererManager
{
public:
	static RendererManager &instance();

	RendererManager(const RendererManager &) = delete;
	RendererManager &operator=(const RendererManager &) = delete;

	void init(); // GL state setup; call after the window/context exists.

	// Camera bookkeeping. The first camera registered becomes the main camera.
	void register_camera(CameraComponent *cam);
	void unregister_camera(CameraComponent *cam);
	void set_main_camera(CameraComponent *cam) { m_main_camera = cam; }
	CameraComponent *main_camera() const { return m_main_camera; }

	// World-space bounding sphere of the scene's renderers (from last frame), used
	// by the cinematic demo camera to frame whatever scene is loaded. False until a
	// frame with geometry has been rendered.
	bool scene_bounds(glm::vec3 &center, float &radius) const
	{
		if (!m_scene_bounds_valid)
			return false;
		center = (m_scene_min + m_scene_max) * 0.5f;
		radius = glm::length(m_scene_max - m_scene_min) * 0.5f;
		return radius > 1e-3f;
	}

	// Drawable bookkeeping.
	void register_renderer(RendererComponent *r);
	void unregister_renderer(RendererComponent *r);

	// Light bookkeeping.
	void register_light(LightComponent *l);
	void unregister_light(LightComponent *l);

	// Sky bookkeeping (first registered is the active environment).
	void register_sky(SkyComponent *s);
	void unregister_sky(SkyComponent *s);

	// Particle emitter bookkeeping (drawn after the transparent pass).
	void register_particles(ParticleComponent *p);
	void unregister_particles(ParticleComponent *p);

	void render_frame();

	// --- Frame profiler -----------------------------------------------------
	// Per-pass CPU + GPU timing. GPU timings come from GL_TIME_ELAPSED queries
	// double-buffered across frames, so reading them back never stalls the
	// pipeline (the values shown are 2 frames old, imperceptible for a HUD).
	enum ProfStage
	{
		PROF_SHADOWS,  // shadow-map depth passes
		PROF_CAPTURES, // env cubemap + reflection captures
		PROF_SCENE,    // main opaque/transparent/particle draw
		PROF_POST,     // SSAO + bloom + composite + overlays
		PROF_COUNT
	};
	struct StageTime
	{
		double cpu_ms = 0.0; // CPU time spent issuing the pass's GL commands
		double gpu_ms = 0.0; // GPU time executing them (timer query)
	};
	const StageTime *stage_times() const { return m_stage_time; }
	static const char *stage_name(int stage);

	// Cast a ray from the main camera through screen pixel (mouseX, mouseY,
	// top-left origin) and return the nearest drawable Object hit, or nullptr.
	class Object *pick(double mouse_x, double mouse_y) const;

	// World-space ray from the main camera through screen pixel (mouseX, mouseY;
	// top-left origin). Returns false if there is no camera. Used for cloth grab.
	bool screen_ray(double mouse_x, double mouse_y, glm::vec3 &origin, glm::vec3 &dir) const;

	glm::vec3 clear_color{0.06f, 0.07f, 0.09f};

	// Directional shadow mapping controls.
	float shadow_strength = 0.85f;   // 0 = off
	float shadow_ortho_size = 14.0f; // half-extent of the light's orthographic box

	// When set, shadow maps are only re-rendered when the occluders or lights
	// actually change (a per-frame fingerprint detects this), so a static scene
	// viewed from a moving camera pays ~0 for shadows. Turn off to force every
	// frame (e.g. if a shader-driven deformation the fingerprint can't see moves).
	bool shadows_auto_skip = true;
	bool shadows_updated_last() const { return m_shadows_updated_last; }

	// Selection outline (drawn around the currently-selected object).
	glm::vec3 outline_color{1.0f, 0.45f, 0.08f};
	float outline_thickness = 0.012f; // ~screen-constant width

	// Debug: draw every collider as a green wireframe overlay.
	bool show_colliders = false;

	// Post-processing (HDR pipeline: bloom + SSAO + ACES tonemap + vignette).
	struct PostSettings
	{
		bool enabled = true; // master toggle (off -> plain tonemap only)
		bool bloom = true;
		float bloom_threshold = 1.0f; // luminance above which pixels bloom
		float bloom_intensity = 0.6f;
		bool ssao = true;
		float ssao_radius = 0.6f;
		float ssao_strength = 1.3f;
		float ssao_bias = 0.025f;
		float exposure = 1.0f;
		float vignette = 0.25f;
		bool fxaa = true; // post-process anti-aliasing (final LDR pass)
		int msaa = 4;     // scene MSAA sample count (1 = off, 2, 4); lower = faster
		bool dof = false;             // depth-of-field
		float dof_focus = 12.0f;      // view-space distance kept sharp
		float dof_range = 10.0f;      // ramp-to-full-blur distance
		float dof_radius = 6.0f;      // max bokeh radius in pixels
		bool ssr = false;             // screen-space reflections
		float ssr_intensity = 0.6f;   // reflection strength
		int ssr_steps = 32;           // ray-march steps (quality vs cost)
		bool fog = false; // depth-based distance fog
		glm::vec3 fog_color = {0.5f, 0.55f, 0.65f};
		float fog_density = 0.03f;
	} post;

	// Defaults handed to freshly-created components (e.g. from the editor's
	// "Add Component"), so a new renderer/material is immediately drawable.
	void set_default_shader(std::shared_ptr<Shader> s) { m_default_shader = std::move(s); }
	void set_default_mesh(std::shared_ptr<Mesh> m) { m_default_mesh = std::move(m); }
	const std::shared_ptr<Shader> &default_shader() const { return m_default_shader; }
	const std::shared_ptr<Mesh> &default_mesh() const { return m_default_mesh; }
	const std::shared_ptr<Shader> &water_shader() const { return m_water_shader; }
	const std::shared_ptr<Shader> &terrain_shader() const { return m_terrain_shader; }

private:
	RendererManager() = default;

	void init_shadow_maps();
	void render_shadow_maps(); // fills shadow indices + m_lightSpace2D, renders depth
	void draw_scene(const RenderContext &ctx, RendererComponent *skip = nullptr,
	                bool main_pass = false);
	void resolve_scene_depth(int w, int h); // MSAA depth -> sampleable texture (for water foam)

	// Pack the pass-constant data (view/proj/lights/shadows/sky/fog) into the
	// shared std140 uniform buffer, uploaded once per draw_scene instead of once
	// per submesh. Bound to uniform-block binding 0; read by basic/water/terrain.
	void init_frame_ubo();
	void upload_frame_ubo(const RenderContext &ctx);
	unsigned int m_frame_ubo = 0;

	// Post-processing: an HDR scene target + SSAO/bloom/composite passes.
	void init_post();
	void ensure_post_targets(int w, int h);     // (re)allocate targets on resize
	void render_post(const RenderContext &ctx); // SSAO + bloom + tonemap -> screen
	void draw_fullscreen();                     // fullscreen triangle (post.vert)

	void init_particles();                                 // quad + instance buffers + shader
	void draw_particles(const RenderContext &ctx);         // billboard every registered emitter
	void draw_selection_outline(const RenderContext &ctx); // stencil hull around the selection
	void draw_colliders(const RenderContext &ctx);         // green wireframe collider overlay
	void init_environment();  // FBO, cube VAO, sky shaders, env cubemap
	void build_environment(); // (re)render the env cubemap if the sky changed
	void render_skybox(const RenderContext &ctx);
	void init_reflection();
	void ensure_reflection_cube(RendererComponent *r);
	void capture_reflections(RenderContext base);

	// Planar reflections for flat mirror surfaces (a reflective Plane mesh):
	// render the scene from a camera mirrored across the plane into a texture the
	// mirror samples by screen position -- an exact, undistorted reflection.
	bool is_planar_mirror(RendererComponent *r) const;
	void ensure_planar_tex(RendererComponent *r, int w, int h);
	void capture_planar_reflections(RenderContext base);

	// Box mirrors (a reflective Cube mesh): one planar reflection per visible face,
	// stored in a 6-layer array the shader indexes by face normal.
	bool is_box_mirror(RendererComponent *r) const;
	void ensure_box_array(RendererComponent *r, int w, int h);
	void capture_box_reflections(RenderContext base);

	CameraComponent *m_main_camera = nullptr;
	std::vector<CameraComponent *> m_cameras;
	std::vector<RendererComponent *> m_renderers;
	std::vector<LightComponent *> m_lights;
	std::vector<SkyComponent *> m_skies;
	std::vector<ParticleComponent *> m_particles;
	std::vector<GpuLight> m_gpu_lights;

	// Particle billboards: a shared unit quad + a re-uploaded per-emitter instance
	// buffer (pos, size, rgba), drawn instanced with the particle shader.
	unsigned int m_particle_vao = 0, m_particle_quad_vbo = 0, m_particle_instance_vbo = 0;
	std::shared_ptr<Shader> m_particle_shader;

	// Sky / IBL environment cubemap (built from a procedural gradient or an
	// equirectangular image; mipmapped so rough surfaces sample blurrier).
	unsigned int m_env_fbo = 0;
	unsigned int m_env_depth = 0;
	unsigned int m_env_cube = 0;
	int m_env_size = 256;
	int m_env_mips = 9; // log2(256)+1
	unsigned int m_cube_vao = 0;
	unsigned int m_cube_vbo = 0;
	std::shared_ptr<Shader> m_sky_capture_shader;  // procedural gradient -> cube face
	std::shared_ptr<Shader> m_equirect_shader;     // equirect image -> cube face
	std::shared_ptr<Shader> m_cross_shader;        // horizontal-cross image -> cube face
	std::shared_ptr<Shader> m_sphere_shader;       // sphere (mirror-ball) image -> cube face
	std::shared_ptr<Shader> m_skybox_shader;       // render the env cube as background
	std::shared_ptr<Shader> m_outline_shader;      // selection outline hull
	std::shared_ptr<Shader> m_water_shader;        // animated water surface
	std::shared_ptr<Shader> m_terrain_shader;      // procedural heightmap terrain
	unsigned int m_depth_fbo = 0, m_depth_tex = 0; // resolved scene depth (for water foam)
	int m_depth_w = 0, m_depth_h = 0;

	// Post-processing targets (recreated on resize). The scene renders into an HDR
	// MSAA buffer, resolves to m_sceneTex, then SSAO/bloom/composite run on it.
	unsigned int m_scene_fbo = 0; // MSAA: RGBA16F colour + depth24_stencil8
	unsigned int m_scene_color_ms = 0, m_scene_depth_ms = 0;
	unsigned int m_resolve_fbo = 0, m_scene_tex = 0; // single-sample resolved HDR colour
	unsigned int m_ssao_fbo = 0, m_ssao_tex = 0;     // AO (RGBA8) + ping-pong blur
	unsigned int m_ssao_fb_o2 = 0, m_ssao_tex2 = 0;
	unsigned int m_bloom_fbo[2] = {0, 0}, m_bloom_tex[2] = {0, 0}; // half-res RGBA16F
	unsigned int m_ldr_fbo = 0, m_ldr_tex = 0; // composite output when FXAA is on (RGBA8)
	unsigned int m_dof_fbo = 0, m_dof_tex = 0; // depth-of-field output (RGBA16F HDR)
	unsigned int m_ssr_fbo = 0, m_ssr_tex = 0; // screen-space reflections output (RGBA16F HDR)
	int m_post_w = 0, m_post_h = 0;
	int m_msaa_allocated = 0;  // sample count the scene targets were built with
	unsigned int m_fs_vao = 0; // empty VAO for the fullscreen triangle
	std::shared_ptr<Shader> m_ssao_shader, m_blur_shader, m_bright_shader, m_composite_shader;
	std::shared_ptr<Shader> m_fxaa_shader, m_dof_shader, m_ssr_shader;
	bool m_env_built = false; // dirty tracking for the env build
	glm::vec3 m_env_zenith{0.0f}, m_env_horizon{0.0f}, m_env_ground{0.0f};
	std::string m_env_image = "?";

	unsigned int m_refl_fbo = 0;
	unsigned int m_refl_depth = 0;
	int m_refl_size = 256;          // per-object reflection cubemap resolution
	int m_refl_mips = 6;            // mip levels for glossy blur
	int m_refl_budget = 2;          // reflective objects re-captured per frame (round-robin)
	unsigned int m_refl_cursor = 0; // which reflective object to update next

	// Planar reflection: one shared FBO + depth, sized to the window; each mirror
	// owns its color texture (RendererComponent::reflectionTex).
	unsigned int m_planar_fbo = 0;
	unsigned int m_planar_depth = 0;
	int m_planar_w = 0;
	int m_planar_h = 0;

	std::shared_ptr<Shader> m_default_shader;
	std::shared_ptr<Mesh> m_default_mesh;

	// Shadows: a depth-texture array for directional/spot lights and a
	// cube-map array for point lights, both rendered through one FBO.
	unsigned int m_shadow_fbo = 0;
	unsigned int m_shadow2_d = 0;   // GL_TEXTURE_2D_ARRAY (depth)
	unsigned int m_shadow_cube = 0; // GL_TEXTURE_CUBE_MAP_ARRAY (depth)
	// Transmittance colour maps (paired with the depth maps): white = light passes
	// freely, black = opaque blocks, tinted = coloured glass. Gives translucent,
	// coloured shadows from transparent occluders.
	unsigned int m_shadow2_d_color = 0;
	unsigned int m_shadow_cube_color = 0;
	int m_shadow2_d_size = 2048;
	int m_cube_size = 1024;
	std::shared_ptr<Shader> m_depth_shader;      // 2D (dir/spot)
	std::shared_ptr<Shader> m_cube_depth_shader; // point (linear distance)
	std::vector<glm::mat4> m_light_space2_d;

	// Dirty-tracking for the shadow pass: a fingerprint of everything the shadow
	// maps depend on (occluder transforms/meshes/transmittance + light params).
	// When it matches the previous frame, the GPU shadow render is skipped.
	unsigned long long m_shadow_fp = 0;
	bool m_shadow_valid = false;          // have the maps been rendered at least once?
	bool m_shadows_updated_last = false;  // did we actually re-render them this frame?

	// Last frame's world-space scene AABB (for the cinematic demo camera framing).
	glm::vec3 m_scene_min{0.0f}, m_scene_max{0.0f};
	bool m_scene_bounds_valid = false;

	// Frame profiler state: two query sets ping-ponged by frame parity, so each
	// set's results are read back two frames after they were issued (always ready).
	StageTime m_stage_time[PROF_COUNT];
	unsigned int m_prof_query[2][PROF_COUNT] = {};
	bool m_prof_init = false;
	bool m_prof_have[2] = {false, false}; // has this set been written at least once?
	unsigned long long m_prof_frame = 0;
};

} // namespace cf
