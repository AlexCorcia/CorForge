#include "core/RendererManager.h"

#include "components/CameraComponent.h"
#include "components/ColliderComponent.h"
#include "components/LightComponent.h"
#include "components/MaterialComponent.h"
#include "components/ParticleComponent.h"
#include "components/RendererComponent.h"
#include "components/SkyComponent.h"
#include "core/AssetManager.h"
#include "core/Object.h"
#include "core/ObjectsManager.h"
#include "core/PhysicsManager.h"
#include "core/WindowManager.h"
#include "gfx/Mesh.h"
#include "gfx/Shader.h"
#include "gfx/Texture.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace cf
{

namespace
{

// Slab test: ray (origin o, direction d) vs AABB [bmin, bmax].
// Returns true and sets tHit to the entry distance (or exit, if o is inside).
bool intersect_aabb(const glm::vec3 &o, const glm::vec3 &d, const glm::vec3 &bmin,
                    const glm::vec3 &bmax, float &t_hit)
{
	float tmin = -std::numeric_limits<float>::max();
	float tmax = std::numeric_limits<float>::max();
	for (int i = 0; i < 3; ++i)
	{
		if (std::abs(d[i]) < 1e-8f)
		{
			if (o[i] < bmin[i] || o[i] > bmax[i])
				return false; // parallel & outside
		}
		else
		{
			const float inv = 1.0f / d[i];
			float t1 = (bmin[i] - o[i]) * inv;
			float t2 = (bmax[i] - o[i]) * inv;
			if (t1 > t2)
				std::swap(t1, t2);
			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);
			if (tmin > tmax)
				return false;
		}
	}
	if (tmax < 0.0f)
		return false;                     // box entirely behind the ray
	t_hit = (tmin >= 0.0f) ? tmin : tmax; // entry, or exit if origin is inside
	return true;
}

// Householder reflection matrix across the plane with unit normal n and offset d
// (plane: dot(n,x) + d = 0). glm is column-major: m[col][row].
glm::mat4 reflection_matrix(const glm::vec3 &n, float d)
{
	glm::mat4 m(1.0f);
	m[0][0] = 1.0f - 2.0f * n.x * n.x;
	m[1][0] = -2.0f * n.x * n.y;
	m[2][0] = -2.0f * n.x * n.z;
	m[3][0] = -2.0f * n.x * d;
	m[0][1] = -2.0f * n.x * n.y;
	m[1][1] = 1.0f - 2.0f * n.y * n.y;
	m[2][1] = -2.0f * n.y * n.z;
	m[3][1] = -2.0f * n.y * d;
	m[0][2] = -2.0f * n.x * n.z;
	m[1][2] = -2.0f * n.y * n.z;
	m[2][2] = 1.0f - 2.0f * n.z * n.z;
	m[3][2] = -2.0f * n.z * d;
	return m;
}

} // namespace

RendererManager &RendererManager::instance()
{
	static RendererManager s;
	return s;
}

void RendererManager::init()
{
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_MULTISAMPLE);
	init_shadow_maps();
	m_depth_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/shadow_depth.vert",
	                                          CORFORGE_SHADER_DIR "/shadow_depth.frag");
	m_cube_depth_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/shadow_cube.vert",
	                                               CORFORGE_SHADER_DIR "/shadow_cube.frag");
	init_reflection();
	init_environment();
	m_outline_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/outline.vert",
	                                            CORFORGE_SHADER_DIR "/outline.frag");
	m_water_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/water.vert",
	                                          CORFORGE_SHADER_DIR "/water.frag");
	m_terrain_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/terrain.vert",
	                                            CORFORGE_SHADER_DIR "/terrain.frag");
	init_post();
	init_particles();

	if (GLenum e = glGetError(); e != GL_NO_ERROR)
		std::fprintf(stderr, "[RendererManager] GL error after shadow init: 0x%X\n", e);
}

namespace
{
// 36 cube-corner positions (CCW); local position doubles as a direction.
const float k_cube_verts[] = {
    -1, -1, -1, 1,  1,  -1, 1,  -1, -1, 1, 1,  -1, -1, -1, -1, -1, 1,  -1, -1, -1, 1,  1,
    -1, 1,  1,  1,  1,  1,  1,  1,  -1, 1, 1,  -1, -1, 1,  -1, 1,  1,  -1, 1,  -1, -1, -1,
    -1, -1, -1, -1, -1, -1, 1,  -1, 1,  1, 1,  1,  1,  1,  -1, -1, 1,  1,  -1, 1,  -1, -1,
    1,  1,  1,  1,  -1, 1,  -1, -1, -1, 1, -1, -1, 1,  -1, 1,  1,  -1, 1,  -1, -1, 1,  -1,
    -1, -1, -1, 1,  -1, 1,  1,  1,  1,  1, -1, 1,  1,  1,  -1, 1,  -1, -1, 1,  1,
};
const glm::vec3 k_face_dir[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                 {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
const glm::vec3 k_face_up[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1},
                                {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};

// Extract the 6 frustum planes from a view-projection matrix (Gribb-Hartmann).
// Each plane (a,b,c,d) has an inward normal; a point is inside when
// dot(normal, p) + d >= 0.
void extract_frustum(const glm::mat4 &m, glm::vec4 planes[6])
{
	for (int i = 0; i < 3; ++i)
	{
		const glm::vec4 row(m[0][i], m[1][i], m[2][i], m[3][i]);
		const glm::vec4 w(m[0][3], m[1][3], m[2][3], m[3][3]);
		planes[2 * i] = w + row;     // left / bottom / near
		planes[2 * i + 1] = w - row; // right / top / far
	}
}

// AABB-vs-frustum test using the "positive vertex": if the box corner farthest
// along a plane's inward normal is still behind that plane, the box is fully out.
bool aabb_visible(const glm::vec4 planes[6], const glm::vec3 &mn, const glm::vec3 &mx)
{
	for (int i = 0; i < 6; ++i)
	{
		const glm::vec4 &p = planes[i];
		const glm::vec3 pv(p.x >= 0 ? mx.x : mn.x, p.y >= 0 ? mx.y : mn.y, p.z >= 0 ? mx.z : mn.z);
		if (glm::dot(glm::vec3(p), pv) + p.w < 0.0f)
			return false;
	}
	return true;
}

// World-space AABB of a renderer (local bounds transformed by its model matrix).
// Returns false if the renderer has no geometry (should not be culled).
bool world_aabb(RendererComponent *r, glm::vec3 &mn, glm::vec3 &mx)
{
	glm::vec3 lmn, lmx;
	if (!r->local_bounds(lmn, lmx))
		return false;
	const glm::mat4 m = r->owner()->world_matrix();
	mn = glm::vec3(std::numeric_limits<float>::max());
	mx = glm::vec3(-std::numeric_limits<float>::max());
	for (int c = 0; c < 8; ++c)
	{
		const glm::vec3 corner((c & 1) ? lmx.x : lmn.x, (c & 2) ? lmx.y : lmn.y,
		                       (c & 4) ? lmx.z : lmn.z);
		const glm::vec3 w = glm::vec3(m * glm::vec4(corner, 1.0f));
		mn = glm::min(mn, w);
		mx = glm::max(mx, w);
	}
	return true;
}

// True if the renderer's world AABB intersects the frustum (or has no bounds).
bool in_frustum(const glm::vec4 planes[6], RendererComponent *r)
{
	glm::vec3 mn, mx;
	if (!world_aabb(r, mn, mx))
		return true;
	return aabb_visible(planes, mn, mx);
}
} // namespace

void RendererManager::init_environment()
{
	glGenVertexArrays(1, &m_cube_vao);
	glGenBuffers(1, &m_cube_vbo);
	glBindVertexArray(m_cube_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_cube_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(k_cube_verts), k_cube_verts, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);

	glGenFramebuffers(1, &m_env_fbo);
	glGenRenderbuffers(1, &m_env_depth);
	glBindRenderbuffer(GL_RENDERBUFFER, m_env_depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_env_size, m_env_size);

	glGenTextures(1, &m_env_cube);
	glBindTexture(GL_TEXTURE_CUBE_MAP, m_env_cube);
	glTexStorage2D(GL_TEXTURE_CUBE_MAP, m_env_mips, GL_RGBA16F, m_env_size, m_env_size);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	m_sky_capture_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/cube_capture.vert",
	                                                CORFORGE_SHADER_DIR "/sky_capture.frag");
	m_equirect_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/cube_capture.vert",
	                                             CORFORGE_SHADER_DIR "/equirect.frag");
	m_cross_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/cube_capture.vert",
	                                          CORFORGE_SHADER_DIR "/cross.frag");
	m_sphere_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/cube_capture.vert",
	                                           CORFORGE_SHADER_DIR "/sphere.frag");
	m_skybox_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/skybox.vert",
	                                           CORFORGE_SHADER_DIR "/skybox.frag");
}

// (Re)render the environment cubemap from the active sky -- only when its
// parameters changed (procedural colours or the equirect image name).
void RendererManager::build_environment()
{
	if (m_skies.empty())
	{
		m_env_built = false;
		return;
	}
	SkyComponent *s = m_skies.front();

	if (m_env_built && s->zenith == m_env_zenith && s->horizon == m_env_horizon &&
	    s->ground == m_env_ground && s->image_name == m_env_image)
		return; // unchanged
	m_env_zenith = s->zenith;
	m_env_horizon = s->horizon;
	m_env_ground = s->ground;
	m_env_image = s->image_name;
	m_env_built = true;

	std::shared_ptr<Texture> img;
	const bool use_image = (s->image_name != "None");
	if (use_image)
		img = AssetManager::instance().texture(s->image_name);

	// Detect the image layout by aspect ratio:
	//   ~2:1 (wide)   = equirectangular panorama
	//   ~4:3          = horizontal cubemap cross
	//   ~1:1 (square) = sphere map (mirror ball)
	enum
	{
		PROC,
		EQUIRECT,
		CROSS,
		SPHERE
	} mode = PROC;
	if (use_image && img && img->height() > 0)
	{
		const float ar = static_cast<float>(img->width()) / static_cast<float>(img->height());
		if (ar >= 1.7f)
			mode = EQUIRECT;
		else if (ar >= 1.15f)
			mode = CROSS;
		else
			mode = SPHERE; // close to square
	}

	Shader &sh = (mode == EQUIRECT) ? *m_equirect_shader
	             : (mode == CROSS)  ? *m_cross_shader
	             : (mode == SPHERE) ? *m_sphere_shader
	                                : *m_sky_capture_shader;
	sh.use();
	if (mode == EQUIRECT)
	{
		img->bind(0);
		sh.set_int("uEquirect", 0);
	}
	else if (mode == CROSS)
	{
		img->bind(0);
		sh.set_int("uCross", 0);
	}
	else if (mode == SPHERE)
	{
		img->bind(0);
		sh.set_int("uSphere", 0);
	}
	else
	{
		sh.set_vec3("uZenith", s->zenith);
		sh.set_vec3("uHorizon", s->horizon);
		sh.set_vec3("uGround", s->ground);
	}
	const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

	glBindFramebuffer(GL_FRAMEBUFFER, m_env_fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_env_depth);
	glViewport(0, 0, m_env_size, m_env_size);
	glDisable(GL_DEPTH_TEST);
	for (int f = 0; f < 6; ++f)
	{
		sh.set_mat4("uView", glm::lookAt(glm::vec3(0.0f), k_face_dir[f], k_face_up[f]));
		sh.set_mat4("uProj", proj);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, m_env_cube, 0);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glClear(GL_COLOR_BUFFER_BIT);
		glBindVertexArray(m_cube_vao);
		glDrawArrays(GL_TRIANGLES, 0, 36);
	}
	glBindVertexArray(0);
	glEnable(GL_DEPTH_TEST);
	glBindTexture(GL_TEXTURE_CUBE_MAP, m_env_cube);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP); // mips = cheap prefilter for rough IBL/specular
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RendererManager::render_skybox(const RenderContext &ctx)
{
	if (!ctx.has_sky)
		return;
	glDepthFunc(GL_LEQUAL);
	m_skybox_shader->use();
	m_skybox_shader->set_mat4("uView", glm::mat4(glm::mat3(ctx.view))); // strip translation
	m_skybox_shader->set_mat4("uProj", ctx.proj);
	m_skybox_shader->set_int("uApplyGamma", ctx.apply_gamma ? 1 : 0);
	// Haze the sky toward the horizon in reflection captures (the main view does
	// this in post), so the reflected sky matches the foggy scene.
	m_skybox_shader->set_int("uApplyFog", ctx.apply_fog ? 1 : 0);
	m_skybox_shader->set_vec3("uFogColor", ctx.fog_color);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, ctx.env_cube);
	m_skybox_shader->set_int("uEnv", 0);
	glBindVertexArray(m_cube_vao);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);
	glDepthFunc(GL_LESS);
}

void RendererManager::init_reflection()
{
	glGenFramebuffers(1, &m_refl_fbo);
	glGenRenderbuffers(1, &m_refl_depth);
	glBindRenderbuffer(GL_RENDERBUFFER, m_refl_depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_refl_size, m_refl_size);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void RendererManager::ensure_reflection_cube(RendererComponent *r)
{
	if (r->reflection_cube != 0)
		return;
	glGenTextures(1, &r->reflection_cube);
	glBindTexture(GL_TEXTURE_CUBE_MAP, r->reflection_cube);
	// Allocate a full mip chain so rougher surfaces can sample blurrier mips
	// (textureLod in the shader) instead of a single sharp/blocky level.
	glTexStorage2D(GL_TEXTURE_CUBE_MAP, m_refl_mips, GL_RGBA16F, m_refl_size, m_refl_size);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

// For every reflective object, render the scene into its own cubemap from its
// position (excluding itself), so it mirrors its actual surroundings.
void RendererManager::capture_reflections(RenderContext base)
{
	base.apply_gamma = false;         // linear capture
	base.reflections_enabled = false; // captured objects don't reflect (1 bounce)
	base.proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
	base.apply_fog = post.fog; // captures skip post, so fog the world in-shader here
	base.fog_color = post.fog_color;
	base.fog_density = post.fog_density;

	static const glm::vec3 face_dir[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
	                                      {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
	static const glm::vec3 face_up[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1},
	                                     {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};

	// Gather the curved reflectors (cubemap mirrors) needing an update.
	std::vector<RendererComponent *> refl;
	for (RendererComponent *r : m_renderers)
	{
		if (!r->mesh)
			continue;
		const MaterialComponent *mc = r->owner()->get_component<MaterialComponent>();
		if (!mc || !mc->material.reflective || !mc->material.shader)
			continue;
		if (is_planar_mirror(r) || is_box_mirror(r)) // these use planar reflection, not a cubemap
			continue;
		refl.push_back(r);
	}
	if (refl.empty())
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, m_refl_fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_refl_depth);
	glViewport(0, 0, m_refl_size, m_refl_size);

	// Round-robin: re-capture only a few reflectors per frame and rotate through
	// them. Each cubemap is 6 full scene renders, so updating all of them every
	// frame is the dominant cost; spreading the work keeps it flat (a reflector
	// refreshes every ceil(count/budget) frames, imperceptible for slow motion).
	const int count = static_cast<int>(refl.size());
	const int budget = std::min(m_refl_budget, count);
	for (int k = 0; k < budget; ++k)
	{
		RendererComponent *r = refl[m_refl_cursor % count];
		++m_refl_cursor;

		ensure_reflection_cube(r);
		const glm::vec3 pos = r->owner()->world_position();
		base.camera_pos = pos;

		for (int f = 0; f < 6; ++f)
		{
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			                       GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, r->reflection_cube, 0);
			glClearColor(clear_color.r, clear_color.g, clear_color.b, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			base.view = glm::lookAt(pos, pos + face_dir[f], face_up[f]);
			draw_scene(base, r); // skip self
		}
		// Build the mip chain so glossy (rough) reflections sample blurred mips.
		glBindTexture(GL_TEXTURE_CUBE_MAP, r->reflection_cube);
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool RendererManager::is_planar_mirror(RendererComponent *r) const
{
	if (!r->mesh || !r->submeshes.empty())
		return false;
	const MaterialComponent *mc = r->owner()->get_component<MaterialComponent>();
	return mc && mc->material.reflective && mc->material.reflect_planar && mc->material.shader;
}

void RendererManager::ensure_planar_tex(RendererComponent *r, int w, int h)
{
	if (r->reflection_tex && r->reflection_tex_w == w && r->reflection_tex_h == h)
		return;
	if (r->reflection_tex)
		glDeleteTextures(1, &r->reflection_tex);
	glGenTextures(1, &r->reflection_tex);
	glBindTexture(GL_TEXTURE_2D, r->reflection_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	r->reflection_tex_w = w;
	r->reflection_tex_h = h;
}

// Render the scene mirrored across each flat-mirror plane into that mirror's
// texture. The mirror then samples it by screen position for an exact reflection.
void RendererManager::capture_planar_reflections(RenderContext base)
{
	WindowManager &win = WindowManager::instance();
	const int w = win.width(), h = win.height();
	if (w <= 0 || h <= 0)
		return;

	bool any_mirror = false;
	for (RendererComponent *r : m_renderers)
		if (is_planar_mirror(r))
		{
			any_mirror = true;
			break;
		}
	if (!any_mirror)
		return;

	if (!m_planar_fbo)
		glGenFramebuffers(1, &m_planar_fbo);
	if (!m_planar_depth)
		glGenRenderbuffers(1, &m_planar_depth);
	if (m_planar_w != w || m_planar_h != h)
	{
		glBindRenderbuffer(GL_RENDERBUFFER, m_planar_depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
		m_planar_w = w;
		m_planar_h = h;
	}

	const glm::vec3 cam_pos = base.camera_pos;
	const glm::mat4 main_view = base.view;
	const glm::mat4 main_proj = base.proj;

	base.apply_gamma = false;  // linear capture (mirror re-tonemaps when sampling)
	base.apply_fog = post.fog; // fog the reflected world (captures skip post)
	base.fog_color = post.fog_color;
	base.fog_density = post.fog_density;

	glBindFramebuffer(GL_FRAMEBUFFER, m_planar_fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_planar_depth);
	glViewport(0, 0, w, h);
	glFrontFace(GL_CW); // a reflection flips winding; keep faces front-facing so
	                    // gl_FrontFacing (and thus lit normals) stays correct

	for (RendererComponent *r : m_renderers)
	{
		if (!is_planar_mirror(r))
			continue;
		ensure_planar_tex(r, w, h);

		const glm::mat4 model = r->owner()->world_matrix();
		const glm::mat3 nmat = glm::transpose(glm::inverse(glm::mat3(model)));
		const glm::vec3 p = r->owner()->world_position();
		glm::vec3 n = glm::normalize(nmat * glm::vec3(0.0f, 1.0f, 0.0f));
		if (glm::dot(n, cam_pos - p) < 0.0f)
			n = -n; // face the camera side
		const float d = -glm::dot(n, p);
		const glm::mat4 m = reflection_matrix(n, d);

		base.view = main_view * m;
		base.proj = main_proj;
		base.camera_pos = glm::vec3(m * glm::vec4(cam_pos, 1.0f));
		base.clip_plane = glm::vec4(n, d); // keep the camera side; clip behind the mirror
		base.screen_size = glm::vec2(static_cast<float>(w), static_cast<float>(h));

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
		                       r->reflection_tex, 0);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glClearColor(clear_color.r, clear_color.g, clear_color.b, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_CLIP_DISTANCE0);
		draw_scene(base, r); // skip the mirror itself
		glDisable(GL_CLIP_DISTANCE0);
	}
	glFrontFace(GL_CCW);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool RendererManager::is_box_mirror(RendererComponent *r) const
{
	if (!r->mesh || !r->submeshes.empty())
		return false;
	const MaterialComponent *mc = r->owner()->get_component<MaterialComponent>();
	return mc && mc->material.reflective && mc->material.reflect_box && mc->material.shader;
}

void RendererManager::ensure_box_array(RendererComponent *r, int w, int h)
{
	if (r->reflection_array && r->reflection_array_w == w && r->reflection_array_h == h)
		return;
	if (r->reflection_array)
		glDeleteTextures(1, &r->reflection_array);
	glGenTextures(1, &r->reflection_array);
	glBindTexture(GL_TEXTURE_2D_ARRAY, r->reflection_array);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA16F, w, h, 6, 0, GL_RGBA, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	r->reflection_array_w = w;
	r->reflection_array_h = h;
}

// A box mirror = 6 flat faces. Render a planar reflection for each camera-facing
// face into its own array layer; the shader picks the layer by face normal.
void RendererManager::capture_box_reflections(RenderContext base)
{
	WindowManager &win = WindowManager::instance();
	const int w = win.width(), h = win.height();
	if (w <= 0 || h <= 0)
		return;

	bool any = false;
	for (RendererComponent *r : m_renderers)
		if (is_box_mirror(r))
		{
			any = true;
			break;
		}
	if (!any)
		return;

	if (!m_planar_fbo)
		glGenFramebuffers(1, &m_planar_fbo);
	if (!m_planar_depth)
		glGenRenderbuffers(1, &m_planar_depth);
	if (m_planar_w != w || m_planar_h != h)
	{
		glBindRenderbuffer(GL_RENDERBUFFER, m_planar_depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
		m_planar_w = w;
		m_planar_h = h;
	}

	const glm::vec3 cam_pos = base.camera_pos;
	const glm::mat4 main_view = base.view;
	const glm::mat4 main_proj = base.proj;
	base.apply_gamma = false;
	base.apply_fog = post.fog; // fog the reflected world (captures skip post)
	base.fog_color = post.fog_color;
	base.fog_density = post.fog_density;

	// Unit-cube face normals + centers in local space.
	static const glm::vec3 local_n[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
	                                     {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
	static const glm::vec3 local_c[6] = {{0.5f, 0, 0},  {-0.5f, 0, 0}, {0, 0.5f, 0},
	                                     {0, -0.5f, 0}, {0, 0, 0.5f},  {0, 0, -0.5f}};

	glBindFramebuffer(GL_FRAMEBUFFER, m_planar_fbo);
	glViewport(0, 0, w, h);
	glFrontFace(GL_CW); // reflection flips winding -> keep faces front-facing for lighting

	for (RendererComponent *r : m_renderers)
	{
		if (!is_box_mirror(r))
			continue;
		ensure_box_array(r, w, h);

		const glm::mat4 model = r->owner()->world_matrix();
		const glm::mat3 nmat = glm::transpose(glm::inverse(glm::mat3(model)));

		for (int f = 0; f < 6; ++f)
		{
			const glm::vec3 n = glm::normalize(nmat * local_n[f]);
			const glm::vec3 c = glm::vec3(model * glm::vec4(local_c[f], 1.0f));
			if (glm::dot(n, cam_pos - c) <= 0.0f)
				continue; // face points away from the camera; not visible

			const float d = -glm::dot(n, c);
			const glm::mat4 m = reflection_matrix(n, d);
			base.view = main_view * m;
			base.proj = main_proj;
			base.camera_pos = glm::vec3(m * glm::vec4(cam_pos, 1.0f));
			base.clip_plane = glm::vec4(n, d);
			base.screen_size = glm::vec2(static_cast<float>(w), static_cast<float>(h));

			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, r->reflection_array, 0,
			                          f);
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
			                          m_planar_depth);
			glClearColor(clear_color.r, clear_color.g, clear_color.b, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glEnable(GL_CLIP_DISTANCE0);
			draw_scene(base, r); // skip self
			glDisable(GL_CLIP_DISTANCE0);
		}
	}
	glFrontFace(GL_CCW);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

namespace
{
constexpr int k_max_shadow2_d = 6;   // directional + spot
constexpr int k_max_shadow_cube = 4; // point
} // namespace

void RendererManager::init_shadow_maps()
{
	glGenFramebuffers(1, &m_shadow_fbo);

	// Depth-texture ARRAY for directional/spot lights.
	glGenTextures(1, &m_shadow2_d);
	glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadow2_d);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, m_shadow2_d_size, m_shadow2_d_size,
	             k_max_shadow2_d, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // outside frustum = lit
	glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);

	// Cube-map ARRAY (depth) for point lights.
	glGenTextures(1, &m_shadow_cube);
	glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_shadow_cube);
	glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT24, m_cube_size, m_cube_size,
	             k_max_shadow_cube * 6, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	// Transmittance colour array (2D, dir/spot). Border = white (light passes).
	glGenTextures(1, &m_shadow2_d_color);
	glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadow2_d_color);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, m_shadow2_d_size, m_shadow2_d_size,
	             k_max_shadow2_d, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);

	// Transmittance colour array (cube, point).
	glGenTextures(1, &m_shadow_cube_color);
	glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_shadow_cube_color);
	glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_RGBA8, m_cube_size, m_cube_size,
	             k_max_shadow_cube * 6, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_fbo);
	glReadBuffer(GL_NONE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RendererManager::render_shadow_maps()
{
	m_light_space2_d.clear();
	int n2d = 0, ncube = 0;

	// Assign shadow slots + compute light-space matrices.
	for (GpuLight &g : m_gpu_lights)
	{
		g.shadow2_d_index = -1;
		g.shadow_cube_index = -1;

		if ((g.type == 0 || g.type == 2) && n2d < k_max_shadow2_d)
		{
			glm::mat4 ls;
			if (g.type == 0)
			{ // directional: orthographic, looking at origin
				const glm::vec3 toward = -glm::normalize(g.direction);
				const glm::vec3 pos = toward * 30.0f;
				const glm::vec3 up =
				    (std::abs(toward.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
				const float s = shadow_ortho_size;
				ls = glm::ortho(-s, s, -s, s, 1.0f, 60.0f) * glm::lookAt(pos, glm::vec3(0.0f), up);
			}
			else
			{ // spot: perspective along the cone
				const glm::vec3 dir = glm::normalize(g.direction);
				const glm::vec3 up =
				    (std::abs(dir.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
				const float outer = std::acos(glm::clamp(g.cos_outer, -1.0f, 1.0f));
				const float fov = glm::min(2.0f * outer + glm::radians(4.0f), glm::radians(160.0f));
				ls = glm::perspective(fov, 1.0f, 0.1f, g.range) *
				     glm::lookAt(g.position, g.position + dir, up);
			}
			g.shadow2_d_index = n2d++;
			m_light_space2_d.push_back(ls);
		}
		else if (g.type == 1 && ncube < k_max_shadow_cube)
		{
			g.shadow_cube_index = ncube++;
		}
	}

	if (n2d == 0 && ncube == 0)
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_fbo);

	// Draw every occluder, telling the depth shader each mesh's colour + opacity
	// so it can write the transmittance (how much / what colour light passes).
	auto draw_occluders = [&](Shader &sh, const glm::mat4 &vp)
	{
		glm::vec4 planes[6];
		extract_frustum(vp, planes);
		for (auto *r : m_renderers)
		{
			if (r->submeshes.empty() && !r->mesh)
				continue;
			if (!in_frustum(planes, r))
				continue; // outside this light's view
			sh.set_mat4("uModel", r->owner()->world_matrix());
			if (!r->submeshes.empty())
			{
				for (const auto &sm : r->submeshes)
				{
					if (!sm.mesh)
						continue;
					sh.set_vec3("uOccColor", sm.material.albedo);
					sh.set_float("uOpacity", sm.material.opacity);
					sm.mesh->draw();
				}
			}
			else
			{
				const MaterialComponent *mc = r->owner()->get_component<MaterialComponent>();
				sh.set_vec3("uOccColor", mc ? mc->material.albedo : glm::vec3(1.0f));
				sh.set_float("uOpacity", mc ? mc->material.opacity : 1.0f);
				r->mesh->draw();
			}
		}
	};

	// 2D passes (directional / spot): depth + transmittance colour.
	if (n2d > 0)
	{
		glViewport(0, 0, m_shadow2_d_size, m_shadow2_d_size);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		m_depth_shader->use();
		for (const GpuLight &g : m_gpu_lights)
		{
			if (g.shadow2_d_index < 0)
				continue;
			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadow2_d, 0,
			                          g.shadow2_d_index);
			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_shadow2_d_color, 0,
			                          g.shadow2_d_index);
			glClearColor(1.0f, 1.0f, 1.0f, 1.0f); // white = light passes (no occluder)
			glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
			m_depth_shader->set_mat4("uLightSpaceMatrix", m_light_space2_d[g.shadow2_d_index]);
			draw_occluders(*m_depth_shader, m_light_space2_d[g.shadow2_d_index]);
		}
	}

	// Cube passes (point) -- 6 faces each: depth + transmittance colour.
	if (ncube > 0)
	{
		static const glm::vec3 face_dir[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
		                                      {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
		static const glm::vec3 face_up[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1},
		                                     {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
		glViewport(0, 0, m_cube_size, m_cube_size);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		m_cube_depth_shader->use();
		for (const GpuLight &g : m_gpu_lights)
		{
			if (g.shadow_cube_index < 0)
				continue;
			const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, g.range);
			m_cube_depth_shader->set_vec3("uLightPos", g.position);
			m_cube_depth_shader->set_float("uFar", g.range);
			for (int f = 0; f < 6; ++f)
			{
				const glm::mat4 vp =
				    proj * glm::lookAt(g.position, g.position + face_dir[f], face_up[f]);
				const int layer = g.shadow_cube_index * 6 + f;
				glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadow_cube, 0,
				                          layer);
				glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_shadow_cube_color,
				                          0, layer);
				glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
				glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
				m_cube_depth_shader->set_mat4("uFaceVP", vp);
				draw_occluders(*m_cube_depth_shader, vp);
			}
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RendererManager::register_camera(CameraComponent *cam)
{
	m_cameras.push_back(cam);
	// Rule: if there's no main camera yet, the camera in the scene becomes it.
	if (!m_main_camera)
		m_main_camera = cam;
}

void RendererManager::unregister_camera(CameraComponent *cam)
{
	m_cameras.erase(std::remove(m_cameras.begin(), m_cameras.end(), cam), m_cameras.end());
	if (m_main_camera == cam)
		m_main_camera = m_cameras.empty() ? nullptr : m_cameras.front();
}

void RendererManager::register_renderer(RendererComponent *r)
{
	m_renderers.push_back(r);
}

void RendererManager::unregister_renderer(RendererComponent *r)
{
	m_renderers.erase(std::remove(m_renderers.begin(), m_renderers.end(), r), m_renderers.end());
}

void RendererManager::register_light(LightComponent *l)
{
	m_lights.push_back(l);
}

void RendererManager::unregister_light(LightComponent *l)
{
	m_lights.erase(std::remove(m_lights.begin(), m_lights.end(), l), m_lights.end());
}

void RendererManager::register_sky(SkyComponent *s)
{
	m_skies.push_back(s);
	m_env_built = false; // force a rebuild
}

void RendererManager::unregister_sky(SkyComponent *s)
{
	m_skies.erase(std::remove(m_skies.begin(), m_skies.end(), s), m_skies.end());
	m_env_built = false;
}

void RendererManager::register_particles(ParticleComponent *p)
{
	m_particles.push_back(p);
}

void RendererManager::unregister_particles(ParticleComponent *p)
{
	m_particles.erase(std::remove(m_particles.begin(), m_particles.end(), p), m_particles.end());
}

// Blit the (MSAA) default-framebuffer depth into a single-sample depth texture so
// the water shader can sample the geometry depth behind the surface (shore foam).
void RendererManager::resolve_scene_depth(int w, int h)
{
	if (!m_depth_fbo)
		glGenFramebuffers(1, &m_depth_fbo);
	if (!m_depth_tex)
		glGenTextures(1, &m_depth_tex);
	if (m_depth_w != w || m_depth_h != h)
	{
		// Match the default framebuffer's format (DEPTH24_STENCIL8, since we ask
		// for stencil bits) -- a depth blit only works between matching formats.
		glBindTexture(GL_TEXTURE_2D, m_depth_tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL,
		             GL_UNSIGNED_INT_24_8, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, m_depth_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
		                       m_depth_tex, 0);
		glDrawBuffer(GL_NONE); // depth-only FBO (no colour) -> still complete
		glReadBuffer(GL_NONE);
		m_depth_w = w;
		m_depth_h = h;
	}
	// The scene renders into the HDR MSAA target; resolve its depth and return to it
	// so the transparent (water) pass keeps drawing into the same buffer.
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_depth_fbo);
	glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fbo);
}

void RendererManager::init_post()
{
	glGenVertexArrays(1, &m_fs_vao); // empty VAO; post.vert builds the triangle from IDs
	m_ssao_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/post.vert",
	                                         CORFORGE_SHADER_DIR "/post_ssao.frag");
	m_blur_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/post.vert",
	                                         CORFORGE_SHADER_DIR "/post_blur.frag");
	m_bright_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/post.vert",
	                                           CORFORGE_SHADER_DIR "/post_bright.frag");
	m_composite_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/post.vert",
	                                              CORFORGE_SHADER_DIR "/post_composite.frag");
}

void RendererManager::draw_fullscreen()
{
	glBindVertexArray(m_fs_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
}

void RendererManager::init_particles()
{
	m_particle_shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/particle.vert",
	                                             CORFORGE_SHADER_DIR "/particle.frag");
	// A unit quad (triangle strip) shared by every billboard.
	const float quad[] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
	glGenVertexArrays(1, &m_particle_vao);
	glGenBuffers(1, &m_particle_quad_vbo);
	glGenBuffers(1, &m_particle_instance_vbo);

	glBindVertexArray(m_particle_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_particle_quad_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0); // aCorner
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);

	glBindBuffer(GL_ARRAY_BUFFER, m_particle_instance_vbo);
	const GLsizei stride = sizeof(ParticleInstance); // vec3 pos, float size, vec4 color
	glEnableVertexAttribArray(1);                    // aPos
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
	                      (void *)offsetof(ParticleInstance, pos));
	glVertexAttribDivisor(1, 1);
	glEnableVertexAttribArray(2); // aSize
	glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
	                      (void *)offsetof(ParticleInstance, size));
	glVertexAttribDivisor(2, 1);
	glEnableVertexAttribArray(3); // aColor
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
	                      (void *)offsetof(ParticleInstance, color));
	glVertexAttribDivisor(3, 1);
	glEnableVertexAttribArray(4); // aVel (for velocity-stretched streaks)
	glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride,
	                      (void *)offsetof(ParticleInstance, vel));
	glVertexAttribDivisor(4, 1);
	glBindVertexArray(0);
}

void RendererManager::draw_particles(const RenderContext &ctx)
{
	if (m_particles.empty() || !m_particle_shader)
		return;

	m_particle_shader->use();
	m_particle_shader->set_mat4("uView", ctx.view);
	m_particle_shader->set_mat4("uProj", ctx.proj);
	m_particle_shader->set_vec2("uScreenSize", ctx.screen_size);
	m_particle_shader->set_float("uNear", ctx.near_plane);
	m_particle_shader->set_float("uFar", ctx.far_plane);
	if (ctx.scene_depth)
	{
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, ctx.scene_depth);
		m_particle_shader->set_int("uDepthTex", 0);
	}

	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);   // test against opaque depth, but don't occlude others
	glDisable(GL_CULL_FACE); // billboards are 2-sided (matters in reflection passes)
	glBindVertexArray(m_particle_vao);

	for (ParticleComponent *p : m_particles)
	{
		const auto &insts = p->instances();
		if (insts.empty())
			continue;
		// Soft particles fade against geometry; only when we have a depth texture.
		m_particle_shader->set_int("uSoft", (p->soft && ctx.scene_depth) ? 1 : 0);
		m_particle_shader->set_float("uStretch", p->stretch); // velocity-stretched streaks
		if (p->blend == ParticleComponent::Blend::Additive)
			glBlendFunc(GL_SRC_ALPHA, GL_ONE); // glow
		else
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glBindBuffer(GL_ARRAY_BUFFER, m_particle_instance_vbo);
		glBufferData(GL_ARRAY_BUFFER,
		             static_cast<GLsizeiptr>(insts.size() * sizeof(ParticleInstance)), insts.data(),
		             GL_STREAM_DRAW);
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(insts.size()));
	}

	glBindVertexArray(0);
	glDepthMask(GL_TRUE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
}

void RendererManager::ensure_post_targets(int w, int h)
{
	if (w == m_post_w && h == m_post_h && m_scene_fbo)
		return;
	m_post_w = w;
	m_post_h = h;
	const int hw = std::max(1, w / 2), hh = std::max(1, h / 2);

	auto make_tex = [](unsigned int &tex, GLint internal, int tw, int th, GLenum fmt, GLenum type)
	{
		if (!tex)
			glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, internal, tw, th, 0, fmt, type, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	};

	// MSAA HDR scene target (matches the window's 4x MSAA so the scene stays AA'd).
	if (!m_scene_fbo)
		glGenFramebuffers(1, &m_scene_fbo);
	if (!m_scene_color_ms)
		glGenRenderbuffers(1, &m_scene_color_ms);
	if (!m_scene_depth_ms)
		glGenRenderbuffers(1, &m_scene_depth_ms);
	glBindRenderbuffer(GL_RENDERBUFFER, m_scene_color_ms);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA16F, w, h);
	glBindRenderbuffer(GL_RENDERBUFFER, m_scene_depth_ms);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH24_STENCIL8, w, h);
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
	                          m_scene_color_ms);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
	                          m_scene_depth_ms);

	// Resolved single-sample HDR colour (sampled by bloom + composite).
	if (!m_resolve_fbo)
		glGenFramebuffers(1, &m_resolve_fbo);
	make_tex(m_scene_tex, GL_RGBA16F, w, h, GL_RGBA, GL_FLOAT);
	glBindFramebuffer(GL_FRAMEBUFFER, m_resolve_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_scene_tex, 0);

	// SSAO + a ping-pong blur target (full resolution).
	if (!m_ssao_fbo)
		glGenFramebuffers(1, &m_ssao_fbo);
	if (!m_ssao_fb_o2)
		glGenFramebuffers(1, &m_ssao_fb_o2);
	make_tex(m_ssao_tex, GL_RGBA8, w, h, GL_RGBA, GL_UNSIGNED_BYTE);
	make_tex(m_ssao_tex2, GL_RGBA8, w, h, GL_RGBA, GL_UNSIGNED_BYTE);
	glBindFramebuffer(GL_FRAMEBUFFER, m_ssao_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssao_tex, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, m_ssao_fb_o2);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssao_tex2, 0);

	// Bloom ping-pong (half resolution HDR -> cheaper, naturally softer).
	for (int i = 0; i < 2; ++i)
	{
		if (!m_bloom_fbo[i])
			glGenFramebuffers(1, &m_bloom_fbo[i]);
		make_tex(m_bloom_tex[i], GL_RGBA16F, hw, hh, GL_RGBA, GL_FLOAT);
		glBindFramebuffer(GL_FRAMEBUFFER, m_bloom_fbo[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloom_tex[i],
		                       0);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RendererManager::render_post(const RenderContext &ctx)
{
	WindowManager &win = WindowManager::instance();
	const int w = win.width(), h = win.height();
	const int hw = std::max(1, w / 2), hh = std::max(1, h / 2);

	// Resolve the MSAA HDR colour into a sampleable single-sample texture.
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolve_fbo);
	glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	const bool use_ao = post.enabled && post.ssao;
	const bool use_bloom = post.enabled && post.bloom;

	// --- SSAO: compute at full res, then separable blur (H -> tex2, V -> tex). ---
	if (use_ao)
	{
		glViewport(0, 0, w, h);
		glBindFramebuffer(GL_FRAMEBUFFER, m_ssao_fbo);
		m_ssao_shader->use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_depth_tex);
		m_ssao_shader->set_int("uDepth", 0);
		m_ssao_shader->set_mat4("uProj", ctx.proj);
		m_ssao_shader->set_mat4("uInvProj", glm::inverse(ctx.proj));
		m_ssao_shader->set_vec2("uScreen", glm::vec2((float)w, (float)h));
		m_ssao_shader->set_float("uRadius", post.ssao_radius);
		m_ssao_shader->set_float("uBias", post.ssao_bias);
		m_ssao_shader->set_float("uStrength", post.ssao_strength);
		draw_fullscreen();

		m_blur_shader->use();
		m_blur_shader->set_int("uTex", 0);
		glActiveTexture(GL_TEXTURE0);
		glBindFramebuffer(GL_FRAMEBUFFER, m_ssao_fb_o2);
		glBindTexture(GL_TEXTURE_2D, m_ssao_tex);
		m_blur_shader->set_vec2("uDir", glm::vec2(1.0f / (float)w, 0.0f));
		draw_fullscreen();
		glBindFramebuffer(GL_FRAMEBUFFER, m_ssao_fbo);
		glBindTexture(GL_TEXTURE_2D, m_ssao_tex2);
		m_blur_shader->set_vec2("uDir", glm::vec2(0.0f, 1.0f / (float)h));
		draw_fullscreen(); // final AO in m_ssaoTex
	}

	// --- Bloom: bright-pass then separable Gaussian ping-pong at half res. ---
	int bloom_final = 0;
	if (use_bloom)
	{
		glViewport(0, 0, hw, hh);
		glBindFramebuffer(GL_FRAMEBUFFER, m_bloom_fbo[0]);
		m_bright_shader->use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_scene_tex);
		m_bright_shader->set_int("uScene", 0);
		m_bright_shader->set_float("uThreshold", post.bloom_threshold);
		draw_fullscreen();

		m_blur_shader->use();
		m_blur_shader->set_int("uTex", 0);
		glActiveTexture(GL_TEXTURE0);
		int src = 0;
		bool horiz = true;
		for (int i = 0; i < 10; ++i)
		{
			const int dst = 1 - src;
			glBindFramebuffer(GL_FRAMEBUFFER, m_bloom_fbo[dst]);
			glBindTexture(GL_TEXTURE_2D, m_bloom_tex[src]);
			m_blur_shader->set_vec2("uDir", horiz ? glm::vec2(1.0f / (float)hw, 0.0f)
			                                      : glm::vec2(0.0f, 1.0f / (float)hh));
			draw_fullscreen();
			src = dst;
			horiz = !horiz;
		}
		bloom_final = src;
	}

	// --- Composite: AO * scene + bloom, expose, ACES tonemap, gamma, vignette. ---
	glViewport(0, 0, w, h);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	m_composite_shader->use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_scene_tex);
	m_composite_shader->set_int("uScene", 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, use_bloom ? m_bloom_tex[bloom_final] : m_scene_tex);
	m_composite_shader->set_int("uBloom", 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, use_ao ? m_ssao_tex : m_scene_tex);
	m_composite_shader->set_int("uAO", 2);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, m_depth_tex);
	m_composite_shader->set_int("uDepth", 3);
	m_composite_shader->set_int("uUseBloom", use_bloom ? 1 : 0);
	m_composite_shader->set_int("uUseAO", use_ao ? 1 : 0);
	m_composite_shader->set_int("uUseFog", (post.enabled && post.fog) ? 1 : 0);
	m_composite_shader->set_vec3("uFogColor", post.fog_color);
	m_composite_shader->set_float("uFogDensity", post.fog_density);
	m_composite_shader->set_float("uNear", ctx.near_plane);
	m_composite_shader->set_float("uFar", ctx.far_plane);
	m_composite_shader->set_mat4("uInvProj", glm::inverse(ctx.proj));
	m_composite_shader->set_mat4("uInvView", glm::inverse(ctx.view));
	m_composite_shader->set_float("uBloomIntensity", post.bloom_intensity);
	m_composite_shader->set_float("uExposure", post.exposure);
	m_composite_shader->set_float("uVignette", post.vignette);
	draw_fullscreen();

	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_DEPTH_TEST);
}

void RendererManager::draw_scene(const RenderContext &ctx, RendererComponent *skip, bool main_pass)
{
	// Frustum culling: skip renderers whose world AABB is outside this pass's view
	// frustum. This is the main optimisation for heavy models (e.g. a 100-submesh
	// car) -- in each reflection-cube face and shadow view only a fraction of the
	// scene is visible, so most of its draw calls are avoided.
	glm::vec4 planes[6];
	extract_frustum(ctx.proj * ctx.view, planes);
	auto visible = [&](RendererComponent *r) { return in_frustum(planes, r); };

	// Opaque pass first (depth writes on), then the skybox fills the background.
	for (auto *r : m_renderers)
		if (r != skip && visible(r))
			r->draw(ctx, /*transparentPass=*/false);
	render_skybox(ctx);

	// Resolve the opaque depth so the transparent water can read what's behind it.
	RenderContext tctx = ctx;
	if (main_pass)
	{
		WindowManager &win = WindowManager::instance();
		resolve_scene_depth(win.width(), win.height());
		tctx.scene_depth = m_depth_tex;
	}

	// Transparent pass: alpha-blend over the opaque scene. Gather the visible
	// transparent objects and sort them BACK-TO-FRONT (farthest from the camera
	// first) so coloured glass correctly tints whatever is behind it -- without
	// sorting, draw order is arbitrary and a near pane wouldn't blend over a far
	// one. Keep depth testing (occluded by closer opaque geometry) but no depth
	// writes. Each object draws BACK faces then FRONT faces so a convex shape
	// blends consistently within itself.
	std::vector<RendererComponent *> transp;
	for (auto *r : m_renderers)
		if (r != skip && visible(r) && r->is_transparent())
			transp.push_back(r);
	std::sort(transp.begin(), transp.end(),
	          [&](RendererComponent *a, RendererComponent *b)
	          {
		          const float da = glm::distance(ctx.camera_pos, a->owner()->world_position());
		          const float db = glm::distance(ctx.camera_pos, b->owner()->world_position());
		          return da > db; // farthest first
	          });

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	glEnable(GL_CULL_FACE);
	for (RendererComponent *r : transp)
	{
		glCullFace(GL_FRONT);
		r->draw(tctx, /*transparentPass=*/true); // back faces
		glCullFace(GL_BACK);
		r->draw(tctx, /*transparentPass=*/true); // front faces
	}
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);

	// Particle billboards on top -- in the main pass AND in reflection captures, so
	// they show up in mirrors and on the water. Soft-fade only runs where we have a
	// resolved depth texture (the main pass sets tctx.sceneDepth); reflection passes
	// leave it 0, so particles draw as plain sprites there.
	draw_particles(tctx);
}

void RendererManager::render_frame()
{
	WindowManager &win = WindowManager::instance();

	if (!m_main_camera)
	{
		glViewport(0, 0, win.width(), win.height());
		glClearColor(clear_color.r, clear_color.g, clear_color.b, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		return;
	}

	// Flatten the scene's lights into GPU form.
	constexpr int k_max_lights = 8;
	m_gpu_lights.clear();
	for (LightComponent *lc : m_lights)
	{
		if (static_cast<int>(m_gpu_lights.size()) >= k_max_lights)
			break;
		Object *lo = lc->owner();

		GpuLight g;
		g.type = static_cast<int>(lc->type);
		g.color = lc->color;
		g.intensity = lc->intensity;
		g.position = lo->world_position();
		g.direction = lo->world_forward(); // travel direction (parent-aware)
		g.range = lc->range;
		g.cos_inner = std::cos(glm::radians(lc->inner_angle));
		g.cos_outer = std::cos(glm::radians(lc->outer_angle));
		m_gpu_lights.push_back(g);
	}

	// Pass 1: render every light's shadow map (assigns per-light shadow indices).
	render_shadow_maps();

	// Build the sky/IBL environment cubemap (only re-renders if the sky changed).
	build_environment();

	// Shared context (lights, shadows).
	RenderContext ctx;
	ctx.lights = m_gpu_lights.data();
	ctx.num_lights = static_cast<int>(m_gpu_lights.size());
	ctx.light_space2_d = m_light_space2_d.data();
	ctx.num_shadow2_d = static_cast<int>(m_light_space2_d.size());
	ctx.shadow2_d_array = m_shadow2_d;
	ctx.shadow_cube_array = m_shadow_cube;
	ctx.shadow2_d_color_array = m_shadow2_d_color;
	ctx.shadow_cube_color_array = m_shadow_cube_color;
	ctx.shadow_strength = shadow_strength;
	if (!m_skies.empty() && m_env_built)
	{
		ctx.has_sky = true;
		ctx.env_cube = m_env_cube;
		ctx.env_max_mip = static_cast<float>(m_env_mips - 1);
		ctx.sky_intensity = m_skies.front()->intensity;
	}

	// World-space scene AABB: the proxy box used to parallax-correct cubemap
	// reflections. Built from every renderer's transformed local bounds.
	{
		glm::vec3 mn(std::numeric_limits<float>::max());
		glm::vec3 mx(-std::numeric_limits<float>::max());
		bool any = false;
		for (RendererComponent *r : m_renderers)
		{
			glm::vec3 lmn, lmx;
			if (!r->local_bounds(lmn, lmx))
				continue;
			const glm::mat4 m = r->owner()->world_matrix();
			for (int c = 0; c < 8; ++c)
			{
				const glm::vec3 corner(((c & 1) ? lmx.x : lmn.x), ((c & 2) ? lmx.y : lmn.y),
				                       ((c & 4) ? lmx.z : lmn.z));
				const glm::vec3 w = glm::vec3(m * glm::vec4(corner, 1.0f));
				mn = glm::min(mn, w);
				mx = glm::max(mx, w);
				any = true;
			}
		}
		if (any)
		{
			ctx.refl_box_min = mn;
			ctx.refl_box_max = mx;
		}
	}

	// Main-camera view used by both the planar pass and the final draw.
	ctx.view = m_main_camera->view();
	ctx.proj = m_main_camera->projection(win.aspect());
	ctx.camera_pos = m_main_camera->world_position();
	ctx.screen_size = glm::vec2(static_cast<float>(win.width()), static_cast<float>(win.height()));
	ctx.time = static_cast<float>(glfwGetTime());
	ctx.near_plane = m_main_camera->near_plane;
	ctx.far_plane = m_main_camera->far_plane;

	// Pass 2a: per-object reflection cubemaps (curved reflectors).
	capture_reflections(ctx);
	// Pass 2b: planar reflections (flat mirror planes).
	capture_planar_reflections(ctx);
	// Pass 2c: box reflections (one planar reflection per cube face).
	capture_box_reflections(ctx);

	// Pass 3: main render into the HDR scene target (linear, no tonemap -- the
	// post composite tonemaps once at the end).
	ensure_post_targets(win.width(), win.height());
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fbo);
	glViewport(0, 0, win.width(), win.height());
	glClearColor(clear_color.r, clear_color.g, clear_color.b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	ctx.apply_gamma = false; // scene writes linear HDR; renderPost tonemaps
	draw_scene(ctx, nullptr, /*mainPass=*/true);

	// Pass 4: resolve MSAA + SSAO + bloom + tonemap, composited to the screen.
	render_post(ctx);

	// Pass 5: outline the selected object on top of the finished (sRGB) image.
	draw_selection_outline(ctx);

	// Pass 6: optional collider wireframe overlay.
	if (show_colliders)
		draw_colliders(ctx);
}

// Draw every registered collider as a green wireframe (sphere / box / plane),
// matching the shape the cloth & physics actually collide against. Reuses the
// flat outline shader with thickness 0 and GL_LINE polygon mode; depth test off
// so the cage is visible through the surface it wraps.
void RendererManager::draw_colliders(const RenderContext &ctx)
{
	if (!m_outline_shader)
		return;
	const auto &colliders = PhysicsManager::instance().colliders();
	if (colliders.empty())
		return;

	AssetManager &assets = AssetManager::instance();
	const std::shared_ptr<Mesh> sphere = assets.mesh("Sphere"); // local radius 0.6
	const std::shared_ptr<Mesh> cube = assets.mesh("Cube");     // local half 0.5
	const std::shared_ptr<Mesh> plane = assets.mesh("Plane");

	const Shader &s = *m_outline_shader;
	s.use();
	s.set_mat4("uView", ctx.view);
	s.set_mat4("uProj", ctx.proj);
	s.set_float("uThickness", 0.0f); // no hull extrusion -> plain wireframe
	s.set_vec3("uCenter", glm::vec3(0.0f));
	s.set_vec3("uColor", glm::vec3(0.15f, 1.0f, 0.35f));

	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	for (ColliderComponent *col : colliders)
	{
		Object *o = col->owner();
		const glm::vec3 c = o->transform.position;
		const glm::vec3 sc = o->transform.scale;
		const glm::vec3 e = o->transform.euler_degrees;

		if (col->shape == ColliderComponent::Shape::Sphere && sphere)
		{
			const float r = col->radius * std::max({sc.x, sc.y, sc.z});
			glm::mat4 m = glm::translate(glm::mat4(1.0f), c);
			m = glm::rotate(m, glm::radians(e.y), glm::vec3(0, 1, 0));
			m = glm::rotate(m, glm::radians(e.x), glm::vec3(1, 0, 0));
			m = glm::rotate(m, glm::radians(e.z), glm::vec3(0, 0, 1));
			m = glm::translate(m, col->center * sc); // local off-pivot offset
			m = glm::scale(m, glm::vec3(r / 0.6f));  // 0.6 = sphere mesh radius
			s.set_mat4("uModel", m);
			sphere->draw();
		}
		else if (col->shape == ColliderComponent::Shape::Box && cube)
		{
			glm::mat4 m = glm::translate(glm::mat4(1.0f), c);
			m = glm::rotate(m, glm::radians(e.y), glm::vec3(0, 1, 0));
			m = glm::rotate(m, glm::radians(e.x), glm::vec3(1, 0, 0));
			m = glm::rotate(m, glm::radians(e.z), glm::vec3(0, 0, 1));
			m = glm::translate(m, col->center * sc);          // local off-pivot offset
			m = glm::scale(m, 2.0f * col->half_extents * sc); // cube half 0.5 -> world half
			s.set_mat4("uModel", m);
			cube->draw();
		}
		else if (col->shape == ColliderComponent::Shape::Plane && plane)
		{
			s.set_mat4("uModel", o->transform.matrix()); // grid matches the ground quad
			plane->draw();
		}
	}

	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Stencil-based selection outline: fill the object's silhouette in the stencil
// buffer, then draw a slightly expanded hull of the same geometry only where the
// stencil is NOT set -- leaving a clean coloured border around the shape. Depth
// testing is off so the outline reads clearly even when partly occluded.
void RendererManager::draw_selection_outline(const RenderContext &ctx)
{
	if (!m_outline_shader)
		return;
	Object *sel = ObjectsManager::instance().selected();
	if (!sel)
		return;
	RendererComponent *rc = sel->get_component<RendererComponent>();
	if (!rc)
		return; // lights / empties have no mesh to outline
	glm::vec3 lmn, lmx;
	if (!rc->local_bounds(lmn, lmx))
		return;

	const glm::mat4 model = sel->world_matrix();
	const glm::vec3 center = glm::vec3(model * glm::vec4((lmn + lmx) * 0.5f, 1.0f));

	auto draw_geom = [&]()
	{
		if (!rc->submeshes.empty())
		{
			for (const Submesh &sm : rc->submeshes)
				if (sm.mesh)
					sm.mesh->draw();
		}
		else if (rc->mesh)
		{
			rc->mesh->draw();
		}
	};

	const Shader &s = *m_outline_shader;
	s.use();
	s.set_mat4("uModel", model);
	s.set_mat4("uView", ctx.view);
	s.set_mat4("uProj", ctx.proj);
	s.set_vec3("uCenter", center);
	s.set_vec3("uColor", outline_color);

	glEnable(GL_STENCIL_TEST);
	glClear(GL_STENCIL_BUFFER_BIT);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	glDisable(GL_DEPTH_TEST);

	// Pass A: mark the silhouette (stencil = 1), no colour, no expansion.
	glStencilFunc(GL_ALWAYS, 1, 0xFF);
	glStencilMask(0xFF);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	s.set_float("uThickness", 0.0f);
	draw_geom();

	// Pass B: the expanded hull, drawn only outside the silhouette.
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
	glStencilMask(0x00);
	s.set_float("uThickness", outline_thickness);
	draw_geom();

	// Restore default state.
	glStencilMask(0xFF);
	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glDisable(GL_STENCIL_TEST);
	glEnable(GL_DEPTH_TEST);
}

bool RendererManager::screen_ray(double mouse_x, double mouse_y, glm::vec3 &origin,
                                 glm::vec3 &dir) const
{
	if (!m_main_camera)
		return false;
	WindowManager &win = WindowManager::instance();
	const float w = static_cast<float>(win.width());
	const float h = static_cast<float>(win.height());
	if (w <= 0.0f || h <= 0.0f)
		return false;

	// Screen pixel -> normalized device coords (flip Y: GLFW is top-left).
	const float ndc_x = 2.0f * static_cast<float>(mouse_x) / w - 1.0f;
	const float ndc_y = 1.0f - 2.0f * static_cast<float>(mouse_y) / h;

	const glm::mat4 inv_vp =
	    glm::inverse(m_main_camera->projection(win.aspect()) * m_main_camera->view());
	glm::vec4 near_p = inv_vp * glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f);
	glm::vec4 far_p = inv_vp * glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f);
	near_p /= near_p.w;
	far_p /= far_p.w;

	origin = glm::vec3(near_p);
	dir = glm::normalize(glm::vec3(far_p - near_p));
	return true;
}

Object *RendererManager::pick(double mouse_x, double mouse_y) const
{
	glm::vec3 ray_origin, ray_dir;
	if (!screen_ray(mouse_x, mouse_y, ray_origin, ray_dir))
		return nullptr;

	Object *best = nullptr;
	float best_dist = std::numeric_limits<float>::max();

	for (auto *r : m_renderers)
	{
		glm::vec3 bmin, bmax;
		if (!r->local_bounds(bmin, bmax))
			continue;

		const glm::mat4 model = r->owner()->world_matrix();
		const glm::mat4 inv_model = glm::inverse(model);

		// Bring the ray into the object's local space (handles rot/scale).
		const glm::vec3 lo = glm::vec3(inv_model * glm::vec4(ray_origin, 1.0f));
		const glm::vec3 ld = glm::vec3(inv_model * glm::vec4(ray_dir, 0.0f));

		float t = 0.0f;
		if (!intersect_aabb(lo, ld, bmin, bmax, t))
			continue;

		// Convert the local hit point to world space and compare by distance.
		const glm::vec3 local_hit = lo + ld * t;
		const glm::vec3 world_hit = glm::vec3(model * glm::vec4(local_hit, 1.0f));
		const float dist = glm::length(world_hit - ray_origin);
		if (dist < best_dist)
		{
			best_dist = dist;
			best = r->owner();
		}
	}
	return best;
}

} // namespace cf
