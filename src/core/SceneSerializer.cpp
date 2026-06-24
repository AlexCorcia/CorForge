#include "core/SceneSerializer.h"

#include "components/BoidComponent.h"
#include "components/BuoyancyComponent.h"
#include "components/CameraComponent.h"
#include "components/FlockComponent.h"
#include "components/ColliderComponent.h"
#include "components/FlyCameraController.h"
#include "components/LightComponent.h"
#include "components/MaterialComponent.h"
#include "components/MoverComponent.h"
#include "components/ParticleComponent.h"
#include "components/PhysicsMoverComponent.h"
#include "components/RendererComponent.h"
#include "components/RigidbodyComponent.h"
#include "components/SkyComponent.h"
#include "components/TerrainComponent.h"
#include "components/TerrainPropComponent.h"
#include "components/WaterComponent.h"
#include "core/AssetManager.h"
#include "core/Object.h"
#include "core/ObjectsManager.h"
#include "core/RendererManager.h"
#include "gfx/ModelLoader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace cf
{

namespace
{

const char *k_asset_dir = CORFORGE_ASSET_DIR;

json v3(const glm::vec3 &v)
{
	return json::array({v.x, v.y, v.z});
}
json v4(const glm::vec4 &v)
{
	return json::array({v.x, v.y, v.z, v.w});
}

glm::vec3 to_v3(const json &j, const glm::vec3 &d)
{
	if (j.is_array() && j.size() >= 3)
		return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
	return d;
}

glm::vec4 to_v4(const json &j, const glm::vec4 &d)
{
	if (j.is_array() && j.size() >= 4)
		return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
	return d;
}

// Serialize every supported component on `o` into a JSON array. Shared by the
// scene saver and the prefab saver so both stay in sync.
json serialize_components(Object *o)
{
	json comps = json::array();

	if (auto *r = o->get_component<RendererComponent>())
		comps.push_back({{"type", "Renderer"},
		                 {"mesh", r->mesh_name},
		                 {"modelSource", r->model_source},
		                 {"modelPart", r->model_part}});

	if (auto *m = o->get_component<MaterialComponent>())
	{
		const Material &mat = m->material;
		comps.push_back({
		    {"type", "Material"},
		    {"albedo", v3(mat.albedo)},
		    {"ambient", mat.ambient},
		    {"metallic", mat.metallic},
		    {"roughness", mat.roughness},
		    {"envSpecular", mat.env_specular},
		    {"albedoMap", mat.albedo_map_name},
		    {"normalMap", mat.normal_map_name},
		    {"uvScale", json::array({mat.uv_scale.x, mat.uv_scale.y})},
		    {"reflective", mat.reflective},
		    {"reflectivity", mat.reflectivity},
		    {"reflectPlanar", mat.reflect_planar},
		    {"reflectBox", mat.reflect_box},
		    {"opacity", mat.opacity},
		    {"transparent", mat.transparent},
		});
	}
	if (auto *l = o->get_component<LightComponent>())
		comps.push_back({
		    {"type", "Light"},
		    {"lightType", static_cast<int>(l->type)},
		    {"color", v3(l->color)},
		    {"intensity", l->intensity},
		    {"range", l->range},
		    {"inner", l->inner_angle},
		    {"outer", l->outer_angle},
		});
	if (auto *s = o->get_component<SkyComponent>())
		comps.push_back({
		    {"type", "Sky"},
		    {"zenith", v3(s->zenith)},
		    {"horizon", v3(s->horizon)},
		    {"ground", v3(s->ground)},
		    {"intensity", s->intensity},
		    {"image", s->image_name},
		});
	if (auto *wa = o->get_component<WaterComponent>())
		comps.push_back({{"type", "Water"},
		                 {"color", v3(wa->color)},
		                 {"opacity", wa->opacity},
		                 {"round", wa->round},
		                 {"calm", wa->calm},
		                 {"splash", wa->splash},
		                 {"splashThreshold", wa->splash_threshold},
		                 {"splashAmount", wa->splash_amount}});
	if (auto *te = o->get_component<TerrainComponent>())
		comps.push_back({{"type", "Terrain"},
		                 {"size", te->size},
		                 {"resolution", te->resolution},
		                 {"heightScale", te->height_scale},
		                 {"frequency", te->frequency},
		                 {"octaves", te->octaves},
		                 {"lacunarity", te->lacunarity},
		                 {"gain", te->gain},
		                 {"seed", te->seed},
		                 {"island", te->island},
		                 {"edgeFalloff", te->edge_falloff},
		                 {"donut", te->donut},
		                 {"ringRadius", te->ring_radius},
		                 {"ringWidth", te->ring_width},
		                 {"grassColor", v3(te->grass_color)},
		                 {"lowPoly", te->low_poly},
		                 {"slab", te->slab},
		                 {"baseDepth", te->base_depth},
		                 {"scatter", te->scatter},
		                 {"treeCount", te->tree_count},
		                 {"rockCount", te->rock_count},
		                 {"propScale", te->prop_scale},
		                 {"waterLevel", te->water_level},
		                 {"treeColor", v3(te->tree_color)},
		                 {"treeMinHeight", te->tree_min_height},
		                 {"forestScale", te->forest_scale},
		                 {"collider", te->collider}});
	// Water auto-adds its own splash emitter on load, so don't double-save it.
	if (auto *pa = o->get_component<ParticleComponent>(); pa && !o->get_component<WaterComponent>())
		comps.push_back({
		    {"type", "Particles"},
		    {"mode", static_cast<int>(pa->mode)},
		    {"enabled", pa->enabled},
		    {"rate", pa->rate},
		    {"lifetime", pa->lifetime},
		    {"lifetimeVar", pa->lifetime_var},
		    {"startSpeed", pa->start_speed},
		    {"speedVar", pa->speed_var},
		    {"spread", pa->spread},
		    {"emitDir", v3(pa->emit_dir)},
		    {"shape", static_cast<int>(pa->shape)},
		    {"emitRadius", pa->emit_radius},
		    {"gravity", pa->gravity},
		    {"drag", pa->drag},
		    {"swirl", pa->swirl},
		    {"attraction", pa->attraction},
		    {"stretch", pa->stretch},
		    {"splashOnGround", pa->splash_on_ground},
		    {"groundY", pa->ground_y},
		    {"boltLength", pa->bolt_length},
		    {"boltSegments", pa->bolt_segments},
		    {"boltJitter", pa->bolt_jitter},
		    {"flickerHz", pa->flicker_hz},
		    {"startSize", pa->start_size},
		    {"endSize", pa->end_size},
		    {"startColor", v4(pa->start_color)},
		    {"endColor", v4(pa->end_color)},
		    {"blend", static_cast<int>(pa->blend)},
		    {"soft", pa->soft},
		    {"maxParticles", pa->max_particles},
		});
	if (auto *bu = o->get_component<BuoyancyComponent>())
		comps.push_back({{"type", "Buoyancy"}, {"strength", bu->strength}, {"drag", bu->drag}});
	if (auto *mv = o->get_component<MoverComponent>())
		comps.push_back({
		    {"type", "Mover"},
		    {"axis", v3(mv->axis)},
		    {"distance", mv->distance},
		    {"speed", mv->speed},
		    {"smooth", mv->smooth},
		    {"enabled", mv->enabled},
		    {"origin", v3(mv->origin())},
		});
	if (auto *pm = o->get_component<PhysicsMoverComponent>())
		comps.push_back({
		    {"type", "PhysicsMover"},
		    {"axis", v3(pm->axis)},
		    {"distance", pm->distance},
		    {"speed", pm->speed},
		    {"smooth", pm->smooth},
		    {"enabled", pm->enabled},
		    {"circular", pm->circular},
		    {"origin", v3(pm->origin())},
		});
	if (auto *c = o->get_component<CameraComponent>())
		comps.push_back({{"type", "Camera"},
		                 {"fov", c->fov_degrees},
		                 {"near", c->near_plane},
		                 {"far", c->far_plane}});
	if (auto *f = o->get_component<FlyCameraController>())
		comps.push_back({{"type", "FlyController"},
		                 {"moveSpeed", f->move_speed},
		                 {"sensitivity", f->sensitivity}});
	if (auto *rb = o->get_component<RigidbodyComponent>())
		comps.push_back({
		    {"type", "Rigidbody"},
		    {"mass", rb->mass},
		    {"gravity", rb->use_gravity},
		    {"static", rb->is_static},
		    {"restitution", rb->restitution},
		    {"friction", rb->friction},
		    {"freezeRotation", rb->freeze_rotation},
		});
	if (auto *col = o->get_component<ColliderComponent>();
	    col && col->shape != ColliderComponent::Shape::Heightfield)
		comps.push_back({
		    {"type", "Collider"},
		    {"shape", static_cast<int>(col->shape)},
		    {"radius", col->radius},
		    {"halfExtents", v3(col->half_extents)},
		    {"center", v3(col->center)},
		    {"planeNormal", v3(col->plane_normal)},
		});
	if (auto *fl = o->get_component<FlockComponent>())
		comps.push_back({
		    {"type", "Flock"},
		    {"count", fl->count},
		    {"spawnRadius", fl->spawn_radius},
		    {"bounds", v3(fl->bounds)},
		    {"maxSpeed", fl->max_speed},
		    {"minSpeed", fl->min_speed},
		    {"perception", fl->perception},
		    {"separationDist", fl->separation_dist},
		    {"wSeparation", fl->w_separation},
		    {"wAlignment", fl->w_alignment},
		    {"wCohesion", fl->w_cohesion},
		    {"wBounds", fl->w_bounds},
		    {"wAvoid", fl->w_avoid},
		    {"avoidRadius", fl->avoid_radius},
		    {"wSeek", fl->w_seek},
		    {"targetName", fl->target_name},
		    {"formShape", fl->form_shape},
		    {"wForm", fl->w_form},
		    {"maxForce", fl->max_force},
		    {"boidScale", fl->boid_scale},
		    {"boidColor", v3(fl->boid_color)},
		    {"boidMesh", fl->boid_mesh},
		    {"physics", fl->physics},
		    {"gravity", fl->gravity},
		    {"buoyancy", fl->buoyancy},
		});
	return comps;
}

// Add + configure components on `o` from a JSON array. Shared by the scene loader
// and the prefab instantiator.
void apply_components(Object *o, const json &comps)
{
	auto shader = RendererManager::instance().default_shader();
	AssetManager &assets = AssetManager::instance();
	for (auto &c : comps)
	{
		const std::string type = c.value("type", std::string());
		if (type == "Renderer")
		{
			auto *r = o->add_component<RendererComponent>();
			r->mesh_name = c.value("mesh", std::string("Cube"));
			r->model_source = c.value("modelSource", std::string());
			r->model_part = c.value("modelPart", -1);
			auto to_mat = [&](const ModelPrimitive &prim)
			{
				Material mat;
				mat.shader = shader;
				mat.albedo = prim.albedo;
				mat.metallic = prim.metallic;
				mat.roughness = prim.roughness;
				mat.metallic_roughness_map = prim.metallic_roughness_map;
				mat.opacity = prim.opacity;
				mat.transparent = prim.transparent;
				if (prim.albedo_map)
				{
					mat.albedo_map = prim.albedo_map;
					mat.albedo_map_name = "(model)";
				}
				if (prim.normal_map)
				{
					mat.normal_map = prim.normal_map;
					mat.normal_map_name = "(model)";
				}
				return mat;
			};
			if (!r->model_source.empty())
			{ // re-import model geometry
				r->mesh = nullptr;
				if (r->model_part >= 0)
				{ // a single split-out part
					const auto parts = load_model_parts(assets.model_path(r->model_source));
					if (r->model_part < static_cast<int>(parts.size()))
						r->submeshes.push_back(
						    {parts[r->model_part].mesh, to_mat(parts[r->model_part])});
				}
				else
				{ // the whole model (all submeshes)
					for (const ModelPrimitive &prim :
					     load_any_model(assets.model_path(r->model_source)))
						r->submeshes.push_back({prim.mesh, to_mat(prim)});
				}
			}
			else
			{
				r->mesh = assets.mesh(r->mesh_name);
			}
		}
		else if (type == "Material")
		{
			auto &mat = o->add_component<MaterialComponent>()->material;
			mat.shader = shader;
			mat.albedo = to_v3(c.value("albedo", json()), mat.albedo);
			mat.ambient = c.value("ambient", mat.ambient);
			mat.metallic = c.value("metallic", mat.metallic);
			mat.roughness = c.value("roughness", mat.roughness);
			mat.env_specular = c.value("envSpecular", mat.env_specular);
			mat.albedo_map_name = c.value("albedoMap", std::string("None"));
			if (mat.albedo_map_name != "None" && mat.albedo_map_name != "(model)")
				mat.albedo_map = assets.texture(mat.albedo_map_name);
			mat.normal_map_name = c.value("normalMap", std::string("None"));
			if (mat.normal_map_name != "None" && mat.normal_map_name != "(model)")
				mat.normal_map = assets.texture_linear(mat.normal_map_name);
			if (c.contains("uvScale") && c["uvScale"].is_array())
				mat.uv_scale = {c["uvScale"][0].get<float>(), c["uvScale"][1].get<float>()};
			mat.reflective = c.value("reflective", false);
			mat.reflectivity = c.value("reflectivity", 0.6f);
			mat.reflect_planar = c.value("reflectPlanar", false);
			mat.reflect_box = c.value("reflectBox", false);
			mat.opacity = c.value("opacity", 1.0f);
			mat.transparent = c.value("transparent", mat.opacity < 0.999f);
			if (mat.opacity < 0.999f)
				if (auto *r = o->get_component<RendererComponent>())
					for (Submesh &sm : r->submeshes)
					{
						sm.material.opacity = mat.opacity;
						sm.material.transparent = true;
					}
		}
		else if (type == "Light")
		{
			auto *l = o->add_component<LightComponent>();
			l->type = static_cast<LightComponent::Type>(c.value("lightType", 0));
			l->color = to_v3(c.value("color", json()), l->color);
			l->intensity = c.value("intensity", 1.0f);
			l->range = c.value("range", 12.0f);
			l->inner_angle = c.value("inner", 18.0f);
			l->outer_angle = c.value("outer", 26.0f);
		}
		else if (type == "Sky")
		{
			auto *s = o->add_component<SkyComponent>();
			s->zenith = to_v3(c.value("zenith", json()), s->zenith);
			s->horizon = to_v3(c.value("horizon", json()), s->horizon);
			s->ground = to_v3(c.value("ground", json()), s->ground);
			s->intensity = c.value("intensity", 1.0f);
			s->image_name = c.value("image", std::string("None"));
		}
		else if (type == "Mover")
		{
			auto *mv = o->add_component<MoverComponent>();
			mv->axis = to_v3(c.value("axis", json()), mv->axis);
			mv->distance = c.value("distance", mv->distance);
			mv->speed = c.value("speed", mv->speed);
			mv->smooth = c.value("smooth", mv->smooth);
			mv->enabled = c.value("enabled", mv->enabled);
			if (c.contains("origin"))
				mv->set_origin(to_v3(c["origin"], mv->origin()));
		}
		else if (type == "PhysicsMover")
		{
			auto *pm = o->add_component<PhysicsMoverComponent>();
			pm->axis = to_v3(c.value("axis", json()), pm->axis);
			pm->distance = c.value("distance", pm->distance);
			pm->speed = c.value("speed", pm->speed);
			pm->smooth = c.value("smooth", pm->smooth);
			pm->enabled = c.value("enabled", pm->enabled);
			pm->circular = c.value("circular", pm->circular);
			if (c.contains("origin"))
				pm->set_origin(to_v3(c["origin"], pm->origin()));
		}
		else if (type == "Water")
		{
			auto *wa = o->add_component<WaterComponent>();
			wa->color = to_v3(c.value("color", json()), wa->color);
			wa->opacity = c.value("opacity", wa->opacity);
			wa->round = c.value("round", wa->round);
			wa->calm = c.value("calm", wa->calm);
			wa->splash = c.value("splash", wa->splash);
			wa->splash_threshold = c.value("splashThreshold", wa->splash_threshold);
			wa->splash_amount = c.value("splashAmount", wa->splash_amount);
			wa->rebuild_mesh();
			wa->apply();
		}
		else if (type == "Terrain")
		{
			auto *te = o->add_component<TerrainComponent>();
			te->size = c.value("size", te->size);
			te->resolution = c.value("resolution", te->resolution);
			te->height_scale = c.value("heightScale", te->height_scale);
			te->frequency = c.value("frequency", te->frequency);
			te->octaves = c.value("octaves", te->octaves);
			te->lacunarity = c.value("lacunarity", te->lacunarity);
			te->gain = c.value("gain", te->gain);
			te->seed = c.value("seed", te->seed);
			te->island = c.value("island", te->island);
			te->edge_falloff = c.value("edgeFalloff", te->edge_falloff);
			te->donut = c.value("donut", te->donut);
			te->ring_radius = c.value("ringRadius", te->ring_radius);
			te->ring_width = c.value("ringWidth", te->ring_width);
			te->grass_color = to_v3(c.value("grassColor", json()), te->grass_color);
			te->low_poly = c.value("lowPoly", te->low_poly);
			te->slab = c.value("slab", te->slab);
			te->base_depth = c.value("baseDepth", te->base_depth);
			te->scatter = c.value("scatter", te->scatter);
			te->tree_count = c.value("treeCount", te->tree_count);
			te->rock_count = c.value("rockCount", te->rock_count);
			te->prop_scale = c.value("propScale", te->prop_scale);
			te->water_level = c.value("waterLevel", te->water_level);
			te->tree_color = to_v3(c.value("treeColor", json()), te->tree_color);
			te->tree_min_height = c.value("treeMinHeight", te->tree_min_height);
			te->forest_scale = c.value("forestScale", te->forest_scale);
			te->collider = c.value("collider", te->collider);
			te->regenerate();
		}
		else if (type == "Particles")
		{
			auto *pa = o->add_component<ParticleComponent>();
			pa->mode = static_cast<ParticleComponent::Mode>(c.value("mode", 0));
			pa->enabled = c.value("enabled", pa->enabled);
			pa->rate = c.value("rate", pa->rate);
			pa->swirl = c.value("swirl", pa->swirl);
			pa->attraction = c.value("attraction", pa->attraction);
			pa->stretch = c.value("stretch", pa->stretch);
			pa->splash_on_ground = c.value("splashOnGround", pa->splash_on_ground);
			pa->ground_y = c.value("groundY", pa->ground_y);
			pa->bolt_length = c.value("boltLength", pa->bolt_length);
			pa->bolt_segments = c.value("boltSegments", pa->bolt_segments);
			pa->bolt_jitter = c.value("boltJitter", pa->bolt_jitter);
			pa->flicker_hz = c.value("flickerHz", pa->flicker_hz);
			pa->lifetime = c.value("lifetime", pa->lifetime);
			pa->lifetime_var = c.value("lifetimeVar", pa->lifetime_var);
			pa->start_speed = c.value("startSpeed", pa->start_speed);
			pa->speed_var = c.value("speedVar", pa->speed_var);
			pa->spread = c.value("spread", pa->spread);
			pa->emit_dir = to_v3(c.value("emitDir", json()), pa->emit_dir);
			pa->shape = static_cast<ParticleComponent::Shape>(c.value("shape", 3));
			pa->emit_radius = c.value("emitRadius", pa->emit_radius);
			pa->gravity = c.value("gravity", pa->gravity);
			pa->drag = c.value("drag", pa->drag);
			pa->start_size = c.value("startSize", pa->start_size);
			pa->end_size = c.value("endSize", pa->end_size);
			pa->start_color = to_v4(c.value("startColor", json()), pa->start_color);
			pa->end_color = to_v4(c.value("endColor", json()), pa->end_color);
			pa->blend = static_cast<ParticleComponent::Blend>(c.value("blend", 0));
			pa->soft = c.value("soft", pa->soft);
			pa->max_particles = c.value("maxParticles", pa->max_particles);
		}
		else if (type == "Buoyancy")
		{
			auto *bu = o->add_component<BuoyancyComponent>();
			bu->strength = c.value("strength", bu->strength);
			bu->drag = c.value("drag", bu->drag);
		}
		else if (type == "Camera")
		{
			auto *cam = o->add_component<CameraComponent>();
			cam->fov_degrees = c.value("fov", 60.0f);
			cam->near_plane = c.value("near", 0.1f);
			cam->far_plane = c.value("far", 100.0f);
		}
		else if (type == "FlyController")
		{
			auto *fc = o->add_component<FlyCameraController>();
			fc->move_speed = c.value("moveSpeed", 3.0f);
			fc->sensitivity = c.value("sensitivity", 0.1f);
		}
		else if (type == "Rigidbody")
		{
			auto *rb = o->add_component<RigidbodyComponent>();
			rb->mass = c.value("mass", 1.0f);
			rb->use_gravity = c.value("gravity", true);
			rb->is_static = c.value("static", false);
			rb->restitution = c.value("restitution", 0.4f);
			rb->friction = c.value("friction", 0.5f);
			rb->freeze_rotation = c.value("freezeRotation", false);
		}
		else if (type == "Collider")
		{
			auto *col = o->add_component<ColliderComponent>();
			col->shape = static_cast<ColliderComponent::Shape>(c.value("shape", 0));
			col->radius = c.value("radius", 0.5f);
			col->half_extents = to_v3(c.value("halfExtents", json()), col->half_extents);
			col->center = to_v3(c.value("center", json()), col->center);
			col->plane_normal = to_v3(c.value("planeNormal", json()), col->plane_normal);
		}
		else if (type == "Flock")
		{
			auto *fl = o->add_component<FlockComponent>();
			fl->count = c.value("count", fl->count);
			fl->spawn_radius = c.value("spawnRadius", fl->spawn_radius);
			fl->bounds = to_v3(c.value("bounds", json()), fl->bounds);
			fl->max_speed = c.value("maxSpeed", fl->max_speed);
			fl->min_speed = c.value("minSpeed", fl->min_speed);
			fl->perception = c.value("perception", fl->perception);
			fl->separation_dist = c.value("separationDist", fl->separation_dist);
			fl->w_separation = c.value("wSeparation", fl->w_separation);
			fl->w_alignment = c.value("wAlignment", fl->w_alignment);
			fl->w_cohesion = c.value("wCohesion", fl->w_cohesion);
			fl->w_bounds = c.value("wBounds", fl->w_bounds);
			fl->w_avoid = c.value("wAvoid", fl->w_avoid);
			fl->avoid_radius = c.value("avoidRadius", fl->avoid_radius);
			fl->w_seek = c.value("wSeek", fl->w_seek);
			fl->target_name = c.value("targetName", std::string());
			fl->form_shape = c.value("formShape", fl->form_shape);
			fl->w_form = c.value("wForm", fl->w_form);
			fl->max_force = c.value("maxForce", fl->max_force);
			fl->boid_scale = c.value("boidScale", fl->boid_scale);
			fl->boid_color = to_v3(c.value("boidColor", json()), fl->boid_color);
			fl->boid_mesh = c.value("boidMesh", fl->boid_mesh);
			fl->physics = c.value("physics", fl->physics);
			fl->gravity = c.value("gravity", fl->gravity);
			fl->buoyancy = c.value("buoyancy", fl->buoyancy);
		}
	}
}

} // namespace

std::string Scene::file_path(const std::string &name)
{
	return (fs::path(k_asset_dir) / "scenes" / (name + ".json")).string();
}

bool Scene::remove(const std::string &file_path)
{
	std::error_code ec;
	return fs::remove(file_path, ec) && !ec;
}

std::vector<std::string> Scene::list()
{
	std::vector<std::string> names;
	const fs::path dir = fs::path(k_asset_dir) / "scenes";
	std::error_code ec;
	fs::create_directories(dir, ec);
	for (const auto &e : fs::directory_iterator(dir, ec))
		if (e.is_regular_file() && e.path().extension() == ".json")
			names.push_back(e.path().stem().string());
	std::sort(names.begin(), names.end());
	return names;
}

void Scene::save(const std::string &file_path)
{
	json root;
	root["objects"] = json::array();

	// Collect the objects we actually save (flock boids are regenerated on load, so
	// they're skipped) and index them so a child can reference its parent by position.
	std::vector<Object *> to_save;
	for (const auto &up : ObjectsManager::instance().objects())
		if (!up->get_component<BoidComponent>() && !up->get_component<TerrainPropComponent>())
			to_save.push_back(up.get());
	std::unordered_map<const Object *, int> index_of;
	for (int i = 0; i < static_cast<int>(to_save.size()); ++i)
		index_of[to_save[i]] = i;

	for (Object *o : to_save)
	{
		json jo;
		jo["name"] = o->name();
		if (o->parent() && index_of.count(o->parent()))
			jo["parent"] = index_of[o->parent()];
		jo["transform"] = {
		    {"position", v3(o->transform.position)},
		    {"rotation", v3(o->transform.euler_degrees)},
		    {"scale", v3(o->transform.scale)},
		};
		jo["components"] = serialize_components(o);
		root["objects"].push_back(jo);
	}

	// Per-scene post-processing / atmosphere (global, not per-object).
	{
		const auto &p = RendererManager::instance().post;
		root["post"] = {
		    {"enabled", p.enabled},
		    {"exposure", p.exposure},
		    {"vignette", p.vignette},
		    {"bloom", p.bloom},
		    {"bloomThreshold", p.bloom_threshold},
		    {"bloomIntensity", p.bloom_intensity},
		    {"ssao", p.ssao},
		    {"ssaoRadius", p.ssao_radius},
		    {"ssaoStrength", p.ssao_strength},
		    {"fog", p.fog},
		    {"fogColor", v3(p.fog_color)},
		    {"fogDensity", p.fog_density},
		};
	}

	std::error_code ec;
	fs::create_directories(fs::path(file_path).parent_path(), ec);
	std::ofstream f(file_path);
	if (f)
		f << root.dump(2);
}

bool Scene::load(const std::string &file_path)
{
	std::ifstream f(file_path);
	if (!f)
		return false;
	json root;
	try
	{
		f >> root;
	}
	catch (...)
	{
		return false;
	}

	ObjectsManager &objects = ObjectsManager::instance();
	objects.clear();

	const json &jobjs = root.value("objects", json::array());
	std::vector<Object *> created;
	created.reserve(jobjs.size());
	for (auto &jo : jobjs)
	{
		Object *o = objects.create_object(jo.value("name", std::string("Object")));
		created.push_back(o);
		if (jo.contains("transform"))
		{
			auto &t = jo["transform"];
			o->transform.position = to_v3(t.value("position", json()), {});
			o->transform.euler_degrees = to_v3(t.value("rotation", json()), {});
			o->transform.scale = to_v3(t.value("scale", json()), glm::vec3(1.0f));
		}

		apply_components(o, jo.value("components", json::array()));
	}

	// Second pass: link parents (the stored transform is already LOCAL, so don't
	// recompute it -- keepWorld=false).
	for (int i = 0; i < static_cast<int>(jobjs.size()); ++i)
	{
		const int p_idx = jobjs[i].value("parent", -1);
		if (p_idx >= 0 && p_idx < static_cast<int>(created.size()) && p_idx != i)
			created[i]->set_parent(created[p_idx], /*keepWorld=*/false);
	}

	// Per-scene post-processing / atmosphere (defaults left untouched if absent).
	if (root.contains("post"))
	{
		auto &j = root["post"];
		auto &p = RendererManager::instance().post;
		p.enabled = j.value("enabled", p.enabled);
		p.exposure = j.value("exposure", p.exposure);
		p.vignette = j.value("vignette", p.vignette);
		p.bloom = j.value("bloom", p.bloom);
		p.bloom_threshold = j.value("bloomThreshold", p.bloom_threshold);
		p.bloom_intensity = j.value("bloomIntensity", p.bloom_intensity);
		p.ssao = j.value("ssao", p.ssao);
		p.ssao_radius = j.value("ssaoRadius", p.ssao_radius);
		p.ssao_strength = j.value("ssaoStrength", p.ssao_strength);
		p.fog = j.value("fog", p.fog);
		p.fog_color = to_v3(j.value("fogColor", json()), p.fog_color);
		p.fog_density = j.value("fogDensity", p.fog_density);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Prefabs: a single object subtree saved as a reusable template.
// ---------------------------------------------------------------------------

std::string Prefab::file_path(const std::string &name)
{
	return (fs::path(k_asset_dir) / "prefabs" / (name + ".json")).string();
}

bool Prefab::remove(const std::string &file_path)
{
	std::error_code ec;
	return fs::remove(file_path, ec) && !ec;
}

std::vector<std::string> Prefab::list()
{
	std::vector<std::string> names;
	const fs::path dir = fs::path(k_asset_dir) / "prefabs";
	std::error_code ec;
	fs::create_directories(dir, ec);
	for (const auto &e : fs::directory_iterator(dir, ec))
		if (e.is_regular_file() && e.path().extension() == ".json")
			names.push_back(e.path().stem().string());
	std::sort(names.begin(), names.end());
	return names;
}

bool Prefab::save(Object *root, const std::string &name)
{
	if (!root || name.empty())
		return false;

	// Collect root + descendants (DFS, root first), skipping runtime-generated
	// children (flock boids, terrain props) which regenerate themselves on load.
	std::vector<Object *> subtree;
	std::function<void(Object *)> collect = [&](Object *o)
	{
		if (o->get_component<BoidComponent>() || o->get_component<TerrainPropComponent>())
			return;
		subtree.push_back(o);
		for (Object *ch : o->children())
			collect(ch);
	};
	collect(root);

	std::unordered_map<const Object *, int> index_of;
	for (int i = 0; i < static_cast<int>(subtree.size()); ++i)
		index_of[subtree[i]] = i;

	json jobjs = json::array();
	for (Object *o : subtree)
	{
		json jo;
		jo["name"] = o->name();
		// Parent index WITHIN the prefab; the root (or any parent outside the
		// subtree) has none, so it becomes a top-level object on instantiate.
		if (o->parent() && index_of.count(o->parent()))
			jo["parent"] = index_of[o->parent()];
		jo["transform"] = {
		    {"position", v3(o->transform.position)},
		    {"rotation", v3(o->transform.euler_degrees)},
		    {"scale", v3(o->transform.scale)},
		};
		jo["components"] = serialize_components(o);
		jobjs.push_back(jo);
	}

	json prefab;
	prefab["prefab"] = true;
	prefab["objects"] = jobjs;

	const std::string path = file_path(name);
	std::error_code ec;
	fs::create_directories(fs::path(path).parent_path(), ec);
	std::ofstream f(path);
	if (!f)
		return false;
	f << prefab.dump(2);
	return true;
}

Object *Prefab::instantiate(const std::string &file_path)
{
	std::ifstream f(file_path);
	if (!f)
		return nullptr;
	json root;
	try
	{
		f >> root;
	}
	catch (...)
	{
		return nullptr;
	}

	const json &jobjs = root.value("objects", json::array());
	if (jobjs.empty())
		return nullptr;

	ObjectsManager &objects = ObjectsManager::instance();
	std::vector<Object *> created;
	created.reserve(jobjs.size());
	for (auto &jo : jobjs)
	{
		Object *o = objects.create_object(jo.value("name", std::string("Object")));
		created.push_back(o);
		if (jo.contains("transform"))
		{
			auto &t = jo["transform"];
			o->transform.position = to_v3(t.value("position", json()), {});
			o->transform.euler_degrees = to_v3(t.value("rotation", json()), {});
			o->transform.scale = to_v3(t.value("scale", json()), glm::vec3(1.0f));
		}
		apply_components(o, jo.value("components", json::array()));
	}
	// Link parents within the prefab (stored transforms are LOCAL => keepWorld=false).
	for (int i = 0; i < static_cast<int>(jobjs.size()); ++i)
	{
		const int p_idx = jobjs[i].value("parent", -1);
		if (p_idx >= 0 && p_idx < static_cast<int>(created.size()) && p_idx != i)
			created[i]->set_parent(created[p_idx], /*keepWorld=*/false);
	}

	Object *new_root = created.front();
	objects.set_selected(new_root);
	return new_root;
}

} // namespace cf
