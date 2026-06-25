#include "components/RendererComponent.h"

#include "components/MaterialComponent.h"
#include "core/Object.h"
#include "core/ObjectsManager.h"
#include "core/RendererManager.h"

#include <glm/glm.hpp>

#include <string>

namespace cf
{

namespace
{

// Draw one mesh with one material, using the per-frame RenderContext.
// planarTex != 0 = flat mirror (single plane); planarArray != 0 = box mirror
// (6 faces, faceN holds their world normals). Both sample exact planar reflections.
void draw_submesh(const RenderContext &ctx, const glm::mat4 &model, const glm::mat3 &normal_mat,
                  const Mesh &mesh, const Material &material, unsigned int reflection_cube,
                  const glm::vec3 &probe_pos, unsigned int planar_tex, unsigned int planar_array,
                  const glm::vec3 *face_n)
{
	if (!material.shader)
		return;
	const Shader &s = *material.shader;
	s.use();
	s.set_mat4("uModel", model);
	s.set_mat3("uNormalMatrix", normal_mat);
	// uView / uProj / uViewPos / uNumLights / uLights[] / uLightSpace2D[] now come
	// from the shared FrameBlock UBO (uploaded once per pass in draw_scene), not
	// from per-submesh uniforms -- this is the whole point of the refactor.

	s.set_vec3("uAlbedo", material.albedo);
	s.set_float("uAmbient", material.ambient);
	s.set_float("uMetallic", material.metallic);
	s.set_float("uRoughness", material.roughness);
	s.set_vec2("uUvScale", material.uv_scale);
	s.set_float("uOpacity", material.opacity);
	s.set_float("uCalm", material.calm); // water: flatten waves + bands (puddles)
	// uTime / uNear / uFar / uApplyGamma / uApplyFog / uFogColor / uFogDensity are
	// all pass-constant -> they live in the FrameBlock UBO now.
	glActiveTexture(GL_TEXTURE11);
	glBindTexture(GL_TEXTURE_2D, ctx.scene_depth); // scene depth for water shore foam
	s.set_int("uDepthTex", 11);
	glActiveTexture(GL_TEXTURE0);

	const bool can_reflect = ctx.reflections_enabled && material.reflective;
	int planar_mode = 0;
	if (can_reflect)
	{
		if (planar_array != 0)
			planar_mode = 2; // box: 6-face array
		else if (planar_tex != 0)
			planar_mode = 1; // single plane
	}
	const bool do_reflect = can_reflect && (reflection_cube != 0 || planar_mode != 0);
	s.set_int("uReflective", do_reflect ? 1 : 0);
	s.set_float("uReflectivity", material.reflectivity);
	// uReflProbePos is per-object (where this cubemap was captured); uReflBoxMin
	// and uClipPlane are pass-constant and live in the FrameBlock UBO.
	s.set_vec3("uReflProbePos", probe_pos);

	const auto &tex = material.albedo_map ? material.albedo_map : Texture::white();
	tex->bind(0);
	s.set_int("uAlbedoMap", 0);

	s.set_int("uHasMRMap", material.metallic_roughness_map ? 1 : 0);
	(material.metallic_roughness_map ? material.metallic_roughness_map : Texture::white())->bind(4);
	s.set_int("uMetalRoughMap", 4);

	s.set_int("uHasNormalMap", material.normal_map ? 1 : 0);
	(material.normal_map ? material.normal_map : Texture::white())->bind(9);
	s.set_int("uNormalMap", 9);

	// Sky / IBL environment: uHasSky/uEnvMaxMip/uSkyIntensity/uShadowStrength are
	// in the FrameBlock UBO; only the per-material env-specular scale stays here.
	s.set_float("uEnvSpecular", material.env_specular);
	glActiveTexture(GL_TEXTURE10);
	glBindTexture(GL_TEXTURE_CUBE_MAP, ctx.env_cube);
	s.set_int("uEnvCube", 10);
	glActiveTexture(GL_TEXTURE0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D_ARRAY, ctx.shadow2_d_array);
	s.set_int("uShadow2D", 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, ctx.shadow_cube_array);
	s.set_int("uShadowCube", 2);
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D_ARRAY, ctx.shadow2_d_color_array);
	s.set_int("uShadow2DColor", 7);
	glActiveTexture(GL_TEXTURE8);
	glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, ctx.shadow_cube_color_array);
	s.set_int("uShadowCubeColor", 8);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_CUBE_MAP, reflection_cube);
	s.set_int("uReflCube", 3);

	// Planar mirror: bind its exact reflection texture(s). The sampler units are
	// always assigned (5 = plane, 6 = box array) so the two different sampler
	// types never collide on the same unit, even when unused this draw.
	s.set_int("uPlanarMode", planar_mode);
	s.set_int("uPlanarTex", 5);
	s.set_int("uPlanarArray", 6);
	if (planar_mode == 1)
	{
		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, planar_tex);
	}
	else if (planar_mode == 2)
	{
		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D_ARRAY, planar_array);
		for (int i = 0; i < 6; ++i)
			s.set_vec3("uFaceN[" + std::to_string(i) + "]", face_n ? face_n[i] : glm::vec3(0.0f));
	}
	glActiveTexture(GL_TEXTURE0);

	mesh.draw();
}

} // namespace

RendererComponent::RendererComponent() : mesh(RendererManager::instance().default_mesh()) {}

void RendererComponent::on_attach()
{
	RendererManager::instance().register_renderer(this);
}

RendererComponent::~RendererComponent()
{
	RendererManager::instance().unregister_renderer(this);
	if (reflection_cube)
		glDeleteTextures(1, &reflection_cube);
	if (reflection_tex)
		glDeleteTextures(1, &reflection_tex);
	if (reflection_array)
		glDeleteTextures(1, &reflection_array);
}

void RendererComponent::draw(const RenderContext &ctx, bool transparent_pass) const
{
	const glm::mat4 model = owner()->world_matrix();
	const glm::mat3 normal_mat = glm::transpose(glm::inverse(glm::mat3(model)));
	const glm::vec3 probe_pos = owner()->world_position(); // where the cubemap was captured

	if (!submeshes.empty())
	{
		for (const Submesh &sm : submeshes)
			if (sm.mesh && sm.material.is_transparent() == transparent_pass)
				draw_submesh(ctx, model, normal_mat, *sm.mesh, sm.material, reflection_cube,
				             probe_pos, 0, 0, nullptr);
		return;
	}

	if (!mesh)
		return;
	const MaterialComponent *mc = owner()->get_component<MaterialComponent>();
	if (!mc || !mc->material.shader)
		return;
	if (mc->material.is_transparent() != transparent_pass)
		return;

	// Reflection method is chosen by the material flags (not the mesh), so stale
	// textures from a previous method aren't reused: box array only when
	// reflectBox, planar texture only when reflectPlanar, else the cubemap.
	const unsigned int planar_array = mc->material.reflect_box ? reflection_array : 0;
	const unsigned int planar_tex = mc->material.reflect_planar ? reflection_tex : 0;

	// Box mirror: world normals of the 6 cube faces (+X,-X,+Y,-Y,+Z,-Z) so the
	// shader can pick the right face's planar reflection.
	glm::vec3 face_n[6];
	if (planar_array)
	{
		static const glm::vec3 local_n[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
		                                     {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
		for (int i = 0; i < 6; ++i)
			face_n[i] = glm::normalize(normal_mat * local_n[i]);
	}
	draw_submesh(ctx, model, normal_mat, *mesh, mc->material, reflection_cube, probe_pos,
	             planar_tex, planar_array, planar_array ? face_n : nullptr);
}

bool RendererComponent::is_transparent() const
{
	if (!submeshes.empty())
	{
		for (const Submesh &sm : submeshes)
			if (sm.material.is_transparent())
				return true;
		return false;
	}
	const MaterialComponent *mc = owner()->get_component<MaterialComponent>();
	return mc && mc->material.is_transparent();
}

bool RendererComponent::local_bounds(glm::vec3 &mn, glm::vec3 &mx) const
{
	bool any = false;
	auto acc = [&](const Mesh &m)
	{
		const glm::vec3 a = m.aabb_min(), b = m.aabb_max();
		if (!any)
		{
			mn = a;
			mx = b;
			any = true;
		}
		else
		{
			mn = glm::min(mn, a);
			mx = glm::max(mx, b);
		}
	};
	if (!submeshes.empty())
	{
		for (const Submesh &sm : submeshes)
			if (sm.mesh)
				acc(*sm.mesh);
	}
	else if (mesh)
	{
		acc(*mesh);
	}
	return any;
}

} // namespace cf
