#include "ui/DebugUI.h"

#include "components/CameraComponent.h"
#include "components/ClothComponent.h"
#include "components/ColliderComponent.h"
#include "components/FlyCameraController.h"
#include "components/LightComponent.h"
#include "components/MaterialComponent.h"
#include "components/MoverComponent.h"
#include "components/PhysicsMoverComponent.h"
#include "components/RendererComponent.h"
#include "components/BuoyancyComponent.h"
#include "components/RigidbodyComponent.h"
#include "components/SkyComponent.h"
#include "components/BoidComponent.h"
#include "components/FlockComponent.h"
#include "components/ParticleComponent.h"
#include "components/TerrainComponent.h"
#include "components/WaterComponent.h"
#include "core/AssetManager.h"
#include "core/ComponentRegistry.h"
#include "core/Object.h"
#include "core/ObjectsManager.h"
#include "core/PhysicsManager.h"
#include "core/RendererManager.h"
#include "core/SceneSerializer.h"
#include "core/WindowManager.h"
#include "gfx/ModelLoader.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <imgui.h>
#include <imgui_internal.h> // DockBuilder*
#include <ImGuizmo.h>

#include <cmath>
#include <functional>
#include <string>

namespace cf
{

namespace
{

ImGuizmo::OPERATION g_gizmo_op = ImGuizmo::TRANSLATE;
ImGuizmo::MODE g_gizmo_mode = ImGuizmo::WORLD;

// A spawn point a few metres in front of the main camera, stepped sideways each
// call so newly-added objects appear where you're looking instead of all stacking
// at the origin (e.g. adding several emitters used to pile them on one spot).
glm::vec3 spawn_point(float dist = 6.0f)
{
	static int n = 0;
	glm::vec3 base{0.0f, 1.0f, 0.0f};
	glm::vec3 right{1.0f, 0.0f, 0.0f};
	if (CameraComponent *cam = RendererManager::instance().main_camera())
	{
		Object *o = cam->owner();
		const glm::vec3 fwd = o->world_forward();
		base = o->world_position() + fwd * dist;
		const glm::vec3 flat = glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f));
		if (glm::length(flat) > 1e-3f)
			right = glm::normalize(flat);
	}
	const float step = static_cast<float>(n++ % 5 - 2); // -2..2, cycles
	return base + right * (step * 1.3f);
}

// Create an object with a renderer + material using a named primitive/asset.
Object *spawn_primitive(const char *mesh_name)
{
	ObjectsManager &objects = ObjectsManager::instance();
	Object *o = objects.create_object(mesh_name);
	o->transform.position = spawn_point();

	auto *rc = o->add_component<RendererComponent>();
	rc->mesh = AssetManager::instance().mesh(mesh_name);
	rc->mesh_name = mesh_name;

	auto &mat = o->add_component<MaterialComponent>()->material;
	mat.shader = RendererManager::instance().default_shader();
	mat.albedo = {0.8f, 0.8f, 0.82f};

	// Give it a collider so it's solid (cloth/physics collide with it). onAttach
	// auto-fits the size AND picks the shape from the mesh (sphere/plane/box).
	o->add_component<ColliderComponent>();

	objects.set_selected(o);
	return o;
}

Object *spawn_light(const char *name, LightComponent::Type type)
{
	ObjectsManager &objects = ObjectsManager::instance();
	Object *o = objects.create_object(name);
	if (type == LightComponent::Type::Directional)
		o->transform.euler_degrees = {-50.0f, -30.0f, 0.0f};
	else
		o->transform.position = spawn_point();
	auto *l = o->add_component<LightComponent>();
	l->type = type;
	if (type != LightComponent::Type::Directional)
		l->intensity = 4.0f;
	objects.set_selected(o);
	return o;
}

// Replace the scene with the minimum needed to run + see something: a main camera
// (with fly controls), a ground plane, a lit cube, and a sun so it isn't pitch black.
void new_minimal_scene()
{
	ObjectsManager &objects = ObjectsManager::instance();
	objects.clear();
	auto shader = RendererManager::instance().default_shader();
	auto cube = AssetManager::instance().mesh("Cube");

	Object *cam = objects.create_object("MainCamera");
	cam->transform.position = {4.5f, 3.0f, 6.0f};
	cam->transform.euler_degrees = {-18.0f, -33.0f, 0.0f};
	cam->add_component<CameraComponent>(); // first camera registered -> main camera
	cam->add_component<FlyCameraController>();

	Object *floor = objects.create_object("Floor");
	floor->transform.position = {0.0f, -0.1f, 0.0f};
	floor->transform.scale = {12.0f, 0.2f, 12.0f};
	auto *fr = floor->add_component<RendererComponent>();
	fr->mesh = cube;
	fr->mesh_name = "Cube";
	auto &fm = floor->add_component<MaterialComponent>()->material;
	fm.shader = shader;
	fm.albedo = {0.5f, 0.52f, 0.55f};
	fm.roughness = 0.7f;
	floor->add_component<ColliderComponent>();

	Object *box = objects.create_object("Cube");
	box->transform.position = {0.0f, 1.0f, 0.0f};
	auto *br = box->add_component<RendererComponent>();
	br->mesh = cube;
	br->mesh_name = "Cube";
	auto &bm = box->add_component<MaterialComponent>()->material;
	bm.shader = shader;
	bm.albedo = {0.8f, 0.45f, 0.32f};
	box->add_component<ColliderComponent>();

	Object *sun = objects.create_object("Sun");
	sun->transform.euler_degrees = {-50.0f, -30.0f, 0.0f};
	sun->add_component<LightComponent>(); // directional, default intensity

	objects.set_selected(box);
}

// Clone an object: its transform + a copy of each known component.
Object *duplicate_object(Object *src)
{
	if (!src)
		return nullptr;
	ObjectsManager &objects = ObjectsManager::instance();
	Object *o = objects.create_object(src->name());
	o->transform = src->transform;
	o->transform.position.x += 0.6f; // nudge so it's visible

	if (auto *r = src->get_component<RendererComponent>())
	{
		auto *nr = o->add_component<RendererComponent>();
		nr->mesh = r->mesh;
		nr->mesh_name = r->mesh_name;
		nr->submeshes = r->submeshes; // imported model pieces
		nr->model_source = r->model_source;
		nr->model_part = r->model_part;
	}
	if (auto *m = src->get_component<MaterialComponent>())
		o->add_component<MaterialComponent>()->material = m->material;
	if (auto *l = src->get_component<LightComponent>())
	{
		auto *nl = o->add_component<LightComponent>();
		nl->type = l->type;
		nl->color = l->color;
		nl->intensity = l->intensity;
		nl->range = l->range;
		nl->inner_angle = l->inner_angle;
		nl->outer_angle = l->outer_angle;
	}
	if (auto *sk = src->get_component<SkyComponent>())
	{
		auto *ns = o->add_component<SkyComponent>();
		ns->zenith = sk->zenith;
		ns->horizon = sk->horizon;
		ns->ground = sk->ground;
		ns->intensity = sk->intensity;
		ns->image_name = sk->image_name;
	}
	if (auto *wa = src->get_component<WaterComponent>())
	{
		auto *nw = o->add_component<WaterComponent>(); // onAttach rebuilds the water material
		nw->color = wa->color;
		nw->opacity = wa->opacity;
		nw->round = wa->round;
		nw->calm = wa->calm;
		nw->splash = wa->splash;
		nw->splash_threshold = wa->splash_threshold;
		nw->splash_amount = wa->splash_amount;
		nw->rebuild_mesh();
		nw->apply();
	}
	if (auto *bu = src->get_component<BuoyancyComponent>())
	{
		auto *nb = o->add_component<BuoyancyComponent>();
		nb->strength = bu->strength;
		nb->drag = bu->drag;
	}
	if (auto *pa = src->get_component<ParticleComponent>();
	    pa && !src->get_component<WaterComponent>())
	{
		auto *np = o->add_component<ParticleComponent>();
		np->mode = pa->mode;
		np->enabled = pa->enabled;
		np->rate = pa->rate;
		np->lifetime = pa->lifetime;
		np->lifetime_var = pa->lifetime_var;
		np->start_speed = pa->start_speed;
		np->speed_var = pa->speed_var;
		np->spread = pa->spread;
		np->emit_dir = pa->emit_dir;
		np->shape = pa->shape;
		np->emit_radius = pa->emit_radius;
		np->gravity = pa->gravity;
		np->drag = pa->drag;
		np->swirl = pa->swirl;
		np->attraction = pa->attraction;
		np->stretch = pa->stretch;
		np->splash_on_ground = pa->splash_on_ground;
		np->ground_y = pa->ground_y;
		np->bolt_length = pa->bolt_length;
		np->bolt_segments = pa->bolt_segments;
		np->bolt_jitter = pa->bolt_jitter;
		np->flicker_hz = pa->flicker_hz;
		np->start_size = pa->start_size;
		np->end_size = pa->end_size;
		np->start_color = pa->start_color;
		np->end_color = pa->end_color;
		np->blend = pa->blend;
		np->soft = pa->soft;
		np->max_particles = pa->max_particles;
	}
	if (auto *fl = src->get_component<FlockComponent>())
	{
		auto *nf = o->add_component<FlockComponent>(); // spawns its own boids on first update
		nf->count = fl->count;
		nf->spawn_radius = fl->spawn_radius;
		nf->bounds = fl->bounds;
		nf->max_speed = fl->max_speed;
		nf->min_speed = fl->min_speed;
		nf->perception = fl->perception;
		nf->separation_dist = fl->separation_dist;
		nf->w_separation = fl->w_separation;
		nf->w_alignment = fl->w_alignment;
		nf->w_cohesion = fl->w_cohesion;
		nf->w_bounds = fl->w_bounds;
		nf->w_avoid = fl->w_avoid;
		nf->avoid_radius = fl->avoid_radius;
		nf->w_seek = fl->w_seek;
		nf->target_name = fl->target_name;
		nf->form_shape = fl->form_shape;
		nf->w_form = fl->w_form;
		nf->max_force = fl->max_force;
		nf->boid_scale = fl->boid_scale;
		nf->boid_color = fl->boid_color;
		nf->boid_mesh = fl->boid_mesh;
		nf->physics = fl->physics;
		nf->gravity = fl->gravity;
		nf->buoyancy = fl->buoyancy;
	}
	if (auto *cl = src->get_component<ClothComponent>())
	{
		auto *nc = o->add_component<ClothComponent>();
		nc->cols = cl->cols;
		nc->rows = cl->rows;
		nc->spacing = cl->spacing;
		nc->iterations = cl->iterations;
		nc->stiffness = cl->stiffness;
		nc->bend_stiffness = cl->bend_stiffness;
		nc->spring_damping = cl->spring_damping;
		nc->mass = cl->mass;
		nc->damping = cl->damping;
		nc->gravity_scale = cl->gravity_scale;
		nc->pin_mode = cl->pin_mode;
		nc->slack = cl->slack;
		nc->tearable = cl->tearable;
		nc->tear_factor = cl->tear_factor;
		nc->mark_dirty(); // rebuild the grid at the new position
	}
	if (auto *mv = src->get_component<MoverComponent>())
	{
		auto *nm = o->add_component<MoverComponent>();
		nm->axis = mv->axis;
		nm->distance = mv->distance;
		nm->speed = mv->speed;
		nm->smooth = mv->smooth;
		nm->enabled = mv->enabled;
		nm->recapture(); // anchor the path at the duplicate's position
	}
	if (auto *pm = src->get_component<PhysicsMoverComponent>())
	{
		auto *np = o->add_component<PhysicsMoverComponent>();
		np->axis = pm->axis;
		np->distance = pm->distance;
		np->speed = pm->speed;
		np->smooth = pm->smooth;
		np->enabled = pm->enabled;
		np->circular = pm->circular;
		np->recapture();
	}
	if (auto *rb = src->get_component<RigidbodyComponent>())
	{
		auto *nb = o->add_component<RigidbodyComponent>();
		nb->mass = rb->mass;
		nb->use_gravity = rb->use_gravity;
		nb->is_static = rb->is_static;
		nb->restitution = rb->restitution;
		nb->friction = rb->friction;
		nb->freeze_rotation = rb->freeze_rotation;
	}
	if (auto *col = src->get_component<ColliderComponent>())
	{
		auto *nc = o->add_component<ColliderComponent>(); // onAttach auto-fits; override after
		nc->shape = col->shape;
		nc->radius = col->radius;
		nc->half_extents = col->half_extents;
		nc->plane_normal = col->plane_normal;
		nc->center = col->center;
	}
	if (auto *c = src->get_component<CameraComponent>())
	{
		auto *nc = o->add_component<CameraComponent>();
		nc->fov_degrees = c->fov_degrees;
		nc->near_plane = c->near_plane;
		nc->far_plane = c->far_plane;
	}
	if (auto *f = src->get_component<FlyCameraController>())
	{
		auto *nf = o->add_component<FlyCameraController>();
		nf->move_speed = f->move_speed;
		nf->sprint_mul = f->sprint_mul;
		nf->sensitivity = f->sensitivity;
	}
	objects.set_selected(o);
	return o;
}

// Move the main camera to frame the given object.
void focus_selected(Object *sel)
{
	CameraComponent *cam = RendererManager::instance().main_camera();
	if (!cam || !sel)
		return;
	Object *cam_obj = cam->owner();

	const glm::vec3 target = sel->transform.position;
	glm::vec3 dir = cam_obj->transform.position - target;
	if (glm::length(dir) < 1e-3f)
		dir = glm::vec3(0, 0, 1);
	dir = glm::normalize(dir);

	const float dist = 5.0f;
	cam_obj->transform.position = target + dir * dist;

	const glm::vec3 fwd = -dir; // looking back at the target
	const float pitch = std::asin(glm::clamp(fwd.y, -1.0f, 1.0f));
	const float yaw = std::atan2(fwd.x, -fwd.z);
	cam_obj->transform.euler_degrees = glm::degrees(glm::vec3(pitch, yaw, 0.0f));
}

// Load a model (glTF/obj) into a RendererComponent as submeshes (one per
// material), replacing any single mesh. Returns false if the model didn't load.
bool load_model_into(RendererComponent *rc, const std::string &name)
{
	const std::vector<ModelPrimitive> prims =
	    load_any_model(AssetManager::instance().model_path(name));
	if (prims.empty())
		return false;
	auto shader = RendererManager::instance().default_shader();
	rc->submeshes.clear();
	for (const ModelPrimitive &prim : prims)
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
		rc->submeshes.push_back({prim.mesh, mat});
	}
	rc->mesh = nullptr; // geometry lives in submeshes
	rc->mesh_name = name;
	rc->model_source = name;
	rc->model_part = -1; // whole model (all submeshes)
	return true;
}

void draw_main_menu_bar()
{
	ObjectsManager &objects = ObjectsManager::instance();
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("Scene"))
		{
			if (ImGui::BeginMenu("Add object"))
			{
				if (ImGui::BeginMenu("Objects"))
				{
					if (ImGui::MenuItem("Cube"))
						spawn_primitive("Cube");
					if (ImGui::MenuItem("Sphere"))
						spawn_primitive("Sphere");
					if (ImGui::MenuItem("Plane"))
						spawn_primitive("Plane");
					if (ImGui::MenuItem("Empty"))
						objects.set_selected(objects.create_object("Empty"));
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Lights"))
				{
					if (ImGui::MenuItem("Directional"))
						spawn_light("Directional Light", LightComponent::Type::Directional);
					if (ImGui::MenuItem("Point"))
						spawn_light("Point Light", LightComponent::Type::Point);
					if (ImGui::MenuItem("Spot"))
						spawn_light("Spot Light", LightComponent::Type::Spot);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Physics"))
				{
					if (ImGui::MenuItem("Water"))
					{
						Object *w = objects.create_object("Water");
						w->transform.position = {0.0f, 0.5f, 0.0f};
						w->transform.scale = {20.0f, 1.0f, 20.0f};
						w->add_component<WaterComponent>();
						objects.set_selected(w);
					}
					if (ImGui::MenuItem("Cloth"))
					{
						Object *c = objects.create_object("Cloth");
						c->transform.position = {0.0f, 4.0f, 0.0f};
						c->add_component<ClothComponent>();
						objects.set_selected(c);
					}
					if (ImGui::MenuItem("Flock (boids)"))
					{
						Object *fo = objects.create_object("Flock");
						fo->transform.position = {0.0f, 6.0f, 0.0f};
						fo->add_component<FlockComponent>();
						objects.set_selected(fo);
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Visuals"))
				{
					if (ImGui::MenuItem("Sky"))
					{
						Object *s = objects.create_object("Sky");
						s->add_component<SkyComponent>();
						objects.set_selected(s);
					}
					if (ImGui::MenuItem("Terrain"))
					{
						Object *t = objects.create_object("Terrain");
						t->add_component<TerrainComponent>();
						objects.set_selected(t);
					}
					auto spawn_fx =
					    [&](const char *name, ParticleComponent::Preset p, glm::vec3 dir)
					{
						Object *o = objects.create_object(name);
						o->transform.position = spawn_point();
						o->transform.euler_degrees = dir;
						o->add_component<ParticleComponent>()->apply_preset(p);
						objects.set_selected(o);
					};
					if (ImGui::MenuItem("Particles: Fountain"))
						spawn_fx("Fountain", ParticleComponent::Preset::Fountain, {});
					if (ImGui::MenuItem("Particles: Fire"))
						spawn_fx("Fire", ParticleComponent::Preset::Fire, {});
					if (ImGui::MenuItem("Particles: Smoke"))
						spawn_fx("Smoke", ParticleComponent::Preset::Smoke, {});
					if (ImGui::MenuItem("Particles: Sparks"))
						spawn_fx("Sparks", ParticleComponent::Preset::Sparks, {});
					if (ImGui::MenuItem("Particles: Portal"))
						spawn_fx("Portal", ParticleComponent::Preset::Portal, {});
					if (ImGui::MenuItem("Particles: Lightning"))
						spawn_fx("Lightning", ParticleComponent::Preset::Lightning, {});
					if (ImGui::MenuItem("Particles: Rain"))
						spawn_fx("Rain", ParticleComponent::Preset::Rain, {});
					if (ImGui::MenuItem("Particles: Snow"))
						spawn_fx("Snow", ParticleComponent::Preset::Snow, {});
					if (ImGui::MenuItem("Particles: Embers"))
						spawn_fx("Embers", ParticleComponent::Preset::Embers, {});
					if (ImGui::MenuItem("Particles: Magic"))
						spawn_fx("Magic", ParticleComponent::Preset::Magic, {});
					if (ImGui::MenuItem("Particles: Mist"))
						spawn_fx("Mist", ParticleComponent::Preset::Mist, {});
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Shortcuts"))
		{
			const bool has_sel = objects.selected() != nullptr;
			if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has_sel))
				duplicate_object(objects.selected());
			if (ImGui::MenuItem("Focus selected", "F", false, has_sel))
				focus_selected(objects.selected());
			if (ImGui::MenuItem("Delete selected", "Del", false, has_sel))
				objects.remove_object(objects.selected());
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help"))
		{
			ImGui::SeparatorText("Camera");
			ImGui::MenuItem("Hold RMB: look around", nullptr, false, false);
			ImGui::MenuItem("WASD / Q E: move, Shift: sprint", nullptr, false, false);
			ImGui::SeparatorText("Editing");
			ImGui::MenuItem("Left click: select object", nullptr, false, false);
			ImGui::MenuItem("1 / 2 / 3: move / rotate / scale gizmo", nullptr, false, false);
			ImGui::MenuItem("Ctrl+D: duplicate", nullptr, false, false);
			ImGui::MenuItem("F: focus selected", nullptr, false, false);
			ImGui::MenuItem("Del: delete selected", nullptr, false, false);
			ImGui::MenuItem("Esc: quit", nullptr, false, false);
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}

// A collapsible component block with a "Remove" button on the right.
void component_header(const char *name, bool &remove_me)
{
	ImGui::PushID(name);
	ImGui::SeparatorText(name);
	ImGui::SameLine(ImGui::GetContentRegionMax().x - 60.0f);
	if (ImGui::SmallButton("Remove"))
		remove_me = true;
	ImGui::PopID();
}

void gizmo_toolbar()
{
	if (ImGui::RadioButton("Move (1)", g_gizmo_op == ImGuizmo::TRANSLATE))
		g_gizmo_op = ImGuizmo::TRANSLATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate (2)", g_gizmo_op == ImGuizmo::ROTATE))
		g_gizmo_op = ImGuizmo::ROTATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Scale (3)", g_gizmo_op == ImGuizmo::SCALE))
		g_gizmo_op = ImGuizmo::SCALE;

	bool world = (g_gizmo_mode == ImGuizmo::WORLD);
	if (ImGui::Checkbox("World space", &world))
		g_gizmo_mode = world ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
}

// Draw + apply the transform gizmo for the selected object (over the scene).
void draw_gizmo(Object *o)
{
	CameraComponent *cam = RendererManager::instance().main_camera();
	if (!cam || !o)
		return;

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGuizmo::SetRect(vp->Pos.x, vp->Pos.y, vp->Size.x, vp->Size.y);

	glm::mat4 view = cam->view();
	glm::mat4 proj = cam->projection(WindowManager::instance().aspect());
	glm::mat4 model = o->world_matrix(); // gizmo manipulates WORLD space

	// While the gizmo is in use, make a physics body kinematic so dragging it
	// pushes/throws other objects (cleared as soon as the drag ends).
	if (auto *rb = o->get_component<RigidbodyComponent>())
		rb->grabbed = ImGuizmo::IsUsing();

	if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), g_gizmo_op, g_gizmo_mode,
	                         glm::value_ptr(model)))
	{
		// Convert the edited world matrix back into this object's LOCAL transform.
		if (o->parent())
			model = glm::inverse(o->parent()->world_matrix()) * model;
		// Decompose back into our Transform (YXZ euler, matching Transform::matrix()).
		const glm::vec3 t(model[3]);
		glm::vec3 s(glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
		            glm::length(glm::vec3(model[2])));
		glm::mat4 rot = model;
		rot[3] = glm::vec4(0, 0, 0, 1);
		if (s.x != 0.0f)
			rot[0] /= s.x;
		if (s.y != 0.0f)
			rot[1] /= s.y;
		if (s.z != 0.0f)
			rot[2] /= s.z;

		float y_ang = 0.0f, x_ang = 0.0f, z_ang = 0.0f;
		glm::extractEulerAngleYXZ(rot, y_ang, x_ang, z_ang);

		o->transform.position = t;
		o->transform.scale = s;
		o->transform.euler_degrees = glm::degrees(glm::vec3(x_ang, y_ang, z_ang));
	}
}

void draw_inspector(Object *o)
{
	gizmo_toolbar();
	if (o->parent())
	{
		ImGui::TextDisabled("Parent: %s", o->parent()->name().c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Unparent"))
			o->set_parent(nullptr);
	}
	else
	{
		ImGui::TextDisabled("Parent: <root>  (drag in Hierarchy to re-parent)");
	}
	ImGui::SeparatorText("Transform (local)");
	ImGui::DragFloat3("Position", &o->transform.position.x, 0.05f);
	ImGui::DragFloat3("Rotation", &o->transform.euler_degrees.x, 0.5f);
	ImGui::DragFloat3("Scale", &o->transform.scale.x, 0.05f, 0.01f, 100.0f);

	std::function<void()> deferred; // never destroy a component mid-draw
	AssetManager &assets = AssetManager::instance();

	if (auto *r = o->get_component<RendererComponent>())
	{
		bool rm = false;
		component_header("RendererComponent", rm);
		if (ImGui::BeginCombo("Mesh", r->mesh_name.c_str()))
		{
			int id = 0;
			for (const std::string &name : assets.mesh_names())
			{
				ImGui::PushID(id++); // unique id: names can repeat (mesh vs model)
				if (ImGui::Selectable(name.c_str(),
				                      r->mesh_name == name && r->model_source.empty()))
					if (auto m = assets.mesh(name))
					{
						r->mesh = m;
						r->mesh_name = name;
						r->submeshes.clear();
						r->model_source.clear();
						r->model_part = -1;
						if (auto *c = o->get_component<ColliderComponent>())
							c->fit_to_mesh();
					}
				ImGui::PopID();
			}
			if (!assets.model_names().empty())
			{
				ImGui::Separator();
				ImGui::TextDisabled("Models (with materials)");
				for (const std::string &name : assets.model_names())
				{
					ImGui::PushID(id++);
					if (ImGui::Selectable(name.c_str(),
					                      r->mesh_name == name && !r->model_source.empty()))
					{
						load_model_into(r, name);
						if (auto *c = o->get_component<ColliderComponent>())
							c->fit_to_mesh();
					}
					ImGui::PopID();
				}
			}
			ImGui::EndCombo();
		}
		// A multi-part imported model (e.g. an FBX with several meshes) can be
		// broken apart into one object per piece, each centred on its own pivot.
		if (!r->model_source.empty() && r->submeshes.size() > 1)
		{
			if (ImGui::Button("Split into objects"))
			{
				Object *src = o;
				const std::string name = r->model_source;
				const Transform tr = o->transform;
				deferred = [src, name, tr]
				{
					ObjectsManager &objs = ObjectsManager::instance();
					auto shader = RendererManager::instance().default_shader();
					const std::string path = AssetManager::instance().model_path(name);
					const auto parts = load_model_parts(path);
					for (int part = 0; part < static_cast<int>(parts.size()); ++part)
					{
						const ModelPrimitive &prim = parts[part];
						Object *n = objs.create_object(name + "_" + std::to_string(part + 1));
						n->transform = tr; // keep the model's rotation + scale
						n->transform.position =
						    glm::vec3(tr.matrix() * glm::vec4(prim.offset, 1.0f));
						auto *nr = n->add_component<RendererComponent>();
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
						nr->submeshes.push_back({prim.mesh, mat});
						nr->mesh = nullptr;
						nr->mesh_name = name;
						nr->model_source = name; // so save/load re-imports this part
						nr->model_part = part;
					}
					objs.remove_object(src);
				};
			}
		}
		if (rm)
			deferred = [o] { o->remove_component<RendererComponent>(); };
	}

	if (auto *m = o->get_component<MaterialComponent>())
	{
		bool rm = false;
		component_header("MaterialComponent", rm);
		Material &mat = m->material;

		// Imported models are drawn from per-part SUBMESH materials, not this one. So
		// any property the user CHANGES here is pushed to every submesh; untouched
		// ones keep their per-part value, so textures/colours aren't clobbered.
		RendererComponent *rend = o->get_component<RendererComponent>();
		const bool parts = rend && !rend->submeshes.empty();
		auto each = [&](const std::function<void(Material &)> &f)
		{
			if (parts)
				for (Submesh &sm : rend->submeshes)
					f(sm.material);
		};
		if (parts)
			ImGui::TextDisabled("Imported model: a change applies to all %d parts.",
			                    static_cast<int>(rend->submeshes.size()));

		if (ImGui::ColorEdit3("Albedo", &mat.albedo.x))
			each([&](Material &s) { s.albedo = mat.albedo; });
		if (ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f))
			each([&](Material &s) { s.metallic = mat.metallic; });
		if (ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f))
			each([&](Material &s) { s.roughness = mat.roughness; });
		if (ImGui::SliderFloat("Env reflection", &mat.env_specular, 0.0f,
		                       1.0f)) // sky mirror (0 = matte)
			each([&](Material &s) { s.env_specular = mat.env_specular; });
		if (ImGui::DragFloat("Ambient", &mat.ambient, 0.005f, 0.0f, 1.0f))
			each([&](Material &s) { s.ambient = mat.ambient; });
		if (ImGui::Checkbox("Reflective", &mat.reflective))
			each([&](Material &s) { s.reflective = mat.reflective; });
		if (mat.reflective)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::SliderFloat("##refl", &mat.reflectivity, 0.0f, 1.0f, "%.2f"))
				each([&](Material &s) { s.reflectivity = mat.reflectivity; });

			// Reflection method (mutually exclusive; both off = cubemap).
			ImGui::TextDisabled("Method:");
			bool cubemap = !mat.reflect_planar && !mat.reflect_box;
			auto sync_method = [&]
			{
				each(
				    [&](Material &s)
				    {
					    s.reflect_planar = mat.reflect_planar;
					    s.reflect_box = mat.reflect_box;
				    });
			};
			if (ImGui::RadioButton("Cubemap", cubemap))
			{
				mat.reflect_planar = false;
				mat.reflect_box = false;
				sync_method();
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Planar (flat)", mat.reflect_planar))
			{
				mat.reflect_planar = true;
				mat.reflect_box = false;
				sync_method();
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Box (per-face)", mat.reflect_box))
			{
				mat.reflect_box = true;
				mat.reflect_planar = false;
				sync_method();
			}
		}
		if (ImGui::BeginCombo("Albedo map", mat.albedo_map_name.c_str()))
		{
			if (ImGui::Selectable("None", mat.albedo_map_name == "None"))
			{
				mat.albedo_map.reset();
				mat.albedo_map_name = "None";
				each(
				    [&](Material &s)
				    {
					    s.albedo_map.reset();
					    s.albedo_map_name = "None";
				    });
			}
			for (const std::string &name : assets.texture_names())
				if (ImGui::Selectable(name.c_str(), mat.albedo_map_name == name))
					if (auto t = assets.texture(name))
					{
						mat.albedo_map = t;
						mat.albedo_map_name = name;
						each(
						    [&](Material &s)
						    {
							    s.albedo_map = t;
							    s.albedo_map_name = name;
						    });
					}
			ImGui::EndCombo();
		}
		if (ImGui::BeginCombo("Normal map", mat.normal_map_name.c_str()))
		{
			if (ImGui::Selectable("None", mat.normal_map_name == "None"))
			{
				mat.normal_map.reset();
				mat.normal_map_name = "None";
				each(
				    [&](Material &s)
				    {
					    s.normal_map.reset();
					    s.normal_map_name = "None";
				    });
			}
			for (const std::string &name : assets.texture_names())
				if (ImGui::Selectable(name.c_str(), mat.normal_map_name == name))
					if (auto t = assets.texture_linear(name))
					{
						mat.normal_map = t;
						mat.normal_map_name = name;
						each(
						    [&](Material &s)
						    {
							    s.normal_map = t;
							    s.normal_map_name = name;
						    });
					}
			ImGui::EndCombo();
		}
		if (ImGui::DragFloat2("UV scale", &mat.uv_scale.x, 0.1f, 0.1f, 64.0f))
			each([&](Material &s) { s.uv_scale = mat.uv_scale; });
		if (ImGui::SliderFloat("Opacity", &mat.opacity, 0.0f, 1.0f))
		{
			mat.transparent = mat.opacity < 0.999f;
			each(
			    [&](Material &s)
			    {
				    s.opacity = mat.opacity;
				    s.transparent = mat.transparent;
			    });
		}
		if (rm)
			deferred = [o] { o->remove_component<MaterialComponent>(); };
	}

	if (auto *l = o->get_component<LightComponent>())
	{
		bool rm = false;
		component_header("LightComponent", rm);
		const char *types[] = {"Directional", "Point", "Spot"};
		int t = static_cast<int>(l->type);
		if (ImGui::Combo("Type", &t, types, 3))
			l->type = static_cast<LightComponent::Type>(t);
		ImGui::ColorEdit3("Color", &l->color.x);
		ImGui::DragFloat("Intensity", &l->intensity, 0.05f, 0.0f, 20.0f);
		if (l->type != LightComponent::Type::Directional)
			ImGui::DragFloat("Range", &l->range, 0.5f, 0.5f, 100.0f);
		if (l->type == LightComponent::Type::Spot)
		{
			ImGui::DragFloat("Inner angle", &l->inner_angle, 0.5f, 1.0f, 80.0f);
			ImGui::DragFloat("Outer angle", &l->outer_angle, 0.5f, 1.0f, 89.0f);
		}
		if (l->type != LightComponent::Type::Point)
			ImGui::TextDisabled("Aim with the object's Rotation / gizmo.");
		if (rm)
			deferred = [o] { o->remove_component<LightComponent>(); };
	}

	if (auto *sky = o->get_component<SkyComponent>())
	{
		bool rm = false;
		component_header("SkyComponent", rm);
		const bool procedural = (sky->image_name == "None");
		if (procedural)
		{
			ImGui::ColorEdit3("Zenith", &sky->zenith.x);
			ImGui::ColorEdit3("Horizon", &sky->horizon.x);
			ImGui::ColorEdit3("Ground", &sky->ground.x);
		}
		ImGui::DragFloat("IBL intensity", &sky->intensity, 0.01f, 0.0f, 4.0f);
		if (ImGui::BeginCombo("Environment", sky->image_name.c_str()))
		{
			if (ImGui::Selectable("None (procedural)", sky->image_name == "None"))
				sky->image_name = "None";
			for (const std::string &name : assets.texture_names())
				if (ImGui::Selectable(name.c_str(), sky->image_name == name))
					sky->image_name = name; // equirectangular image as environment
			ImGui::EndCombo();
		}
		ImGui::TextDisabled("Skybox + image-based lighting on all surfaces.");
		if (rm)
			deferred = [o] { o->remove_component<SkyComponent>(); };
	}

	if (auto *wa = o->get_component<WaterComponent>())
	{
		bool rm = false;
		component_header("WaterComponent", rm);
		bool ch = false;
		ch |= ImGui::ColorEdit3("Water color", &wa->color.x);
		ch |= ImGui::SliderFloat("See-through", &wa->opacity, 0.0f, 1.0f);
		ch |= ImGui::SliderFloat("Calm (puddle)", &wa->calm, 0.0f, 1.0f);
		if (ch)
			wa->apply();
		if (ImGui::Checkbox("Round (circular)", &wa->round))
			wa->rebuild_mesh();
		ImGui::Checkbox("Splash on impact", &wa->splash);
		ImGui::BeginDisabled(!wa->splash);
		ImGui::DragFloat("Splash speed min", &wa->splash_threshold, 0.05f, 0.0f, 10.0f);
		ImGui::DragFloat("Splash amount", &wa->splash_amount, 0.05f, 0.0f, 5.0f);
		ImGui::EndDisabled();
		ImGui::TextDisabled("Bodies that break the surface splash automatically.");
		if (rm)
			deferred = [o] { o->remove_component<WaterComponent>(); };
	}

	if (auto *te = o->get_component<TerrainComponent>())
	{
		bool rm = false;
		component_header("TerrainComponent", rm);
		bool ch = false;
		int style = te->low_poly ? 0 : 1;
		ImGui::TextDisabled("Style:");
		ImGui::SameLine();
		if (ImGui::RadioButton("Low-poly", &style, 0))
		{
			te->low_poly = true;
			ch = true;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Smooth", &style, 1))
		{
			te->low_poly = false;
			ch = true;
		}
		ImGui::SetItemTooltip("Low-poly = faceted diorama look; Smooth = rounded realistic.");
		ch |= ImGui::Checkbox("Slab (cliff base)", &te->slab);
		ImGui::SetItemTooltip("Cut a solid block with stratified dirt cliffs + a flat bottom.");
		if (te->slab)
			ch |= ImGui::DragFloat("Base depth", &te->base_depth, 0.1f, 0.5f, 40.0f);
		ImGui::Separator();
		ch |= ImGui::DragFloat("Size", &te->size, 0.5f, 2.0f, 500.0f);
		ch |= ImGui::DragInt("Resolution", &te->resolution, 1.0f, 2, 512);
		ImGui::SetItemTooltip("Grid divisions per side. Higher = smoother but heavier.");
		ch |= ImGui::DragFloat("Height", &te->height_scale, 0.1f, 0.0f, 100.0f);
		ch |= ImGui::DragFloat("Frequency", &te->frequency, 0.001f, 0.001f, 0.5f, "%.3f");
		ImGui::SetItemTooltip("Lower = larger, smoother hills; higher = more, tighter bumps.");
		ch |= ImGui::DragInt("Octaves", &te->octaves, 1.0f, 1, 8);
		ImGui::SetItemTooltip("Layers of detail stacked on top of each other.");
		ch |= ImGui::DragFloat("Lacunarity", &te->lacunarity, 0.05f, 1.0f, 4.0f);
		ch |= ImGui::DragFloat("Gain", &te->gain, 0.02f, 0.0f, 1.0f);
		int seed = static_cast<int>(te->seed);
		if (ImGui::DragInt("Seed", &seed, 1.0f, 0, 1000000))
		{
			te->seed = static_cast<unsigned>(seed);
			ch = true;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Random"))
		{
			te->seed = te->seed * 1664525u + 1013904223u;
			ch = true;
		}
		ch |= ImGui::Checkbox("Island (fade edges down)", &te->island);
		if (te->island)
			ch |= ImGui::SliderFloat("Edge falloff", &te->edge_falloff, 0.0f, 1.0f);
		ch |= ImGui::Checkbox("Donut (ring of land + basin)", &te->donut);
		ImGui::SetItemTooltip("An atoll: a raised ring of land with water in the middle.");
		if (te->donut)
		{
			ch |= ImGui::SliderFloat("Ring radius", &te->ring_radius, 0.1f, 1.0f);
			ch |= ImGui::SliderFloat("Ring width", &te->ring_width, 0.05f, 0.6f);
		}
		ch |= ImGui::ColorEdit3("Grass color", &te->grass_color.x);
		ImGui::Separator();
		ch |= ImGui::Checkbox("Scatter props", &te->scatter);
		ImGui::SetItemTooltip("Low-poly trees + rocks placed by height & slope.");
		if (te->scatter)
		{
			ch |= ImGui::DragInt("Trees", &te->tree_count, 1.0f, 0, 4000);
			ch |= ImGui::DragInt("Rocks", &te->rock_count, 1.0f, 0, 2000);
			ch |= ImGui::DragFloat("Forest clump", &te->forest_scale, 0.2f, 2.0f, 60.0f);
			ImGui::SetItemTooltip("Size of forest groves. Smaller = tighter, scattered clumps; "
			                      "larger = big forests with wide clearings.");
			ch |= ImGui::DragFloat("Prop scale", &te->prop_scale, 0.02f, 0.1f, 5.0f);
			ch |= ImGui::DragFloat("Water level", &te->water_level, 0.1f, -50.0f, 50.0f);
			ImGui::SetItemTooltip(
			    "Props are only placed above this world height (keep them out of water).");
			ch |= ImGui::DragFloat("Tree min height", &te->tree_min_height, 0.1f, 0.0f, 30.0f);
			ImGui::SetItemTooltip(
			    "Trees stay this far above the water — low/flat ground is left bare.");
			ch |= ImGui::ColorEdit3("Tree color", &te->tree_color.x);
		}
		ImGui::Separator();
		if (ImGui::Checkbox("Heightfield collider (physics)", &te->collider))
			ch = true;
		ImGui::SetItemTooltip(
		    "Bodies (boxes, balls) collide with and rest on the terrain surface.");
		if (ch || ImGui::Button("Regenerate"))
			te->regenerate();
		ImGui::TextDisabled("Coloured by height + slope (sand/grass/rock/snow).");
		if (rm)
			deferred = [o] { o->remove_component<TerrainComponent>(); };
	}

	if (auto *bu = o->get_component<BuoyancyComponent>())
	{
		bool rm = false;
		component_header("BuoyancyComponent", rm);
		ImGui::DragFloat("Float strength", &bu->strength, 0.2f, 0.0f, 60.0f);
		ImGui::DragFloat("Water drag", &bu->drag, 0.05f, 0.0f, 10.0f);
		ImGui::TextDisabled("Floats in the scene's water (needs a Rigidbody + Collider).");
		if (rm)
			deferred = [o] { o->remove_component<BuoyancyComponent>(); };
	}

	if (auto *pa = o->get_component<ParticleComponent>())
	{
		bool rm = false;
		component_header("ParticleComponent", rm);
		ImGui::Checkbox("Emitting", &pa->enabled);
		if (ImGui::Button("Burst"))
			pa->burst(40);
		ImGui::TextDisabled("Presets:");
		ImGui::SameLine();
		if (ImGui::SmallButton("Fire"))
			pa->apply_preset(ParticleComponent::Preset::Fire);
		ImGui::SameLine();
		if (ImGui::SmallButton("Smoke"))
			pa->apply_preset(ParticleComponent::Preset::Smoke);
		ImGui::SameLine();
		if (ImGui::SmallButton("Sparks"))
			pa->apply_preset(ParticleComponent::Preset::Sparks);
		ImGui::SameLine();
		if (ImGui::SmallButton("Fountain"))
			pa->apply_preset(ParticleComponent::Preset::Fountain);
		ImGui::SameLine();
		if (ImGui::SmallButton("Portal"))
			pa->apply_preset(ParticleComponent::Preset::Portal);
		ImGui::SameLine();
		if (ImGui::SmallButton("Lightning"))
			pa->apply_preset(ParticleComponent::Preset::Lightning);
		if (ImGui::SmallButton("Rain"))
			pa->apply_preset(ParticleComponent::Preset::Rain);
		ImGui::SameLine();
		if (ImGui::SmallButton("Snow"))
			pa->apply_preset(ParticleComponent::Preset::Snow);
		ImGui::SameLine();
		if (ImGui::SmallButton("Embers"))
			pa->apply_preset(ParticleComponent::Preset::Embers);
		ImGui::SameLine();
		if (ImGui::SmallButton("Magic"))
			pa->apply_preset(ParticleComponent::Preset::Magic);
		ImGui::SameLine();
		if (ImGui::SmallButton("Explosion"))
			pa->apply_preset(ParticleComponent::Preset::Explosion);
		ImGui::SameLine();
		if (ImGui::SmallButton("Mist"))
			pa->apply_preset(ParticleComponent::Preset::Mist);

		const char *modes[] = {"Emitter", "Bolt (lightning)"};
		int md = static_cast<int>(pa->mode);
		if (ImGui::Combo("Mode", &md, modes, 2))
			pa->mode = static_cast<ParticleComponent::Mode>(md);
		const char *blends[] = {"Alpha", "Additive"};
		int bl = static_cast<int>(pa->blend);
		if (ImGui::Combo("Blend", &bl, blends, 2))
			pa->blend = static_cast<ParticleComponent::Blend>(bl);
		ImGui::DragFloat3("Emit dir", &pa->emit_dir.x, 0.02f, -1.0f, 1.0f);
		ImGui::ColorEdit4("Start color", &pa->start_color.x,
		                  ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);

		if (pa->mode == ParticleComponent::Mode::Bolt)
		{
			ImGui::SeparatorText("Lightning bolt");
			ImGui::DragFloat("Length", &pa->bolt_length, 0.1f, 0.2f, 40.0f);
			ImGui::DragInt("Segments", &pa->bolt_segments, 1.0f, 2, 64);
			ImGui::DragFloat("Jitter", &pa->bolt_jitter, 0.02f, 0.0f, 4.0f);
			ImGui::DragFloat("Flicker (Hz)", &pa->flicker_hz, 0.5f, 0.5f, 60.0f);
			ImGui::DragFloat("Glow size", &pa->start_size, 0.005f, 0.01f, 1.0f);
		}
		else
		{
			ImGui::SeparatorText("Emission");
			const char *shapes[] = {"Point", "Disk", "Sphere", "Cone", "Ring"};
			int sh = static_cast<int>(pa->shape);
			if (ImGui::Combo("Shape", &sh, shapes, 5))
				pa->shape = static_cast<ParticleComponent::Shape>(sh);
			ImGui::DragFloat("Rate", &pa->rate, 1.0f, 0.0f, 1000.0f);
			ImGui::DragFloat("Lifetime", &pa->lifetime, 0.02f, 0.05f, 10.0f);
			ImGui::DragFloat("Spread", &pa->spread, 0.5f, 0.0f, 180.0f);
			ImGui::DragFloat("Start speed", &pa->start_speed, 0.1f, 0.0f, 30.0f);
			ImGui::DragFloat("Emit radius", &pa->emit_radius, 0.01f, 0.0f, 5.0f);
			ImGui::DragFloat("Gravity", &pa->gravity, 0.1f, -30.0f, 30.0f);
			ImGui::DragFloat("Drag", &pa->drag, 0.01f, 0.0f, 5.0f);
			ImGui::SeparatorText("Vortex (portals)");
			ImGui::DragFloat("Swirl", &pa->swirl, 0.05f, -30.0f, 30.0f);
			ImGui::DragFloat("Attraction", &pa->attraction, 0.05f, -10.0f, 20.0f);
			ImGui::SeparatorText("Rain / streaks");
			ImGui::DragFloat("Stretch", &pa->stretch, 0.002f, 0.0f, 0.4f);
			ImGui::Checkbox("Ground splash", &pa->splash_on_ground);
			ImGui::BeginDisabled(!pa->splash_on_ground);
			ImGui::DragFloat("Ground Y", &pa->ground_y, 0.02f, -20.0f, 20.0f);
			ImGui::EndDisabled();
			ImGui::SeparatorText("Look");
			ImGui::DragFloat("Start size", &pa->start_size, 0.005f, 0.0f, 3.0f);
			ImGui::DragFloat("End size", &pa->end_size, 0.005f, 0.0f, 3.0f);
			ImGui::ColorEdit4("End color", &pa->end_color.x,
			                  ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
			ImGui::Checkbox("Soft (fade vs geometry)", &pa->soft);
		}
		if (rm)
			deferred = [o] { o->remove_component<ParticleComponent>(); };
	}

	if (auto *fl = o->get_component<FlockComponent>())
	{
		bool rm = false;
		component_header("FlockComponent", rm);
		// Shows a help marker after the previous widget with a hover tooltip.
		auto tip = [](const char *desc)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", desc);
		};

		// --- Always-visible header: live count + the Respawn action ---------
		ImGui::Text("Boids alive: %d", fl->alive());
		if (ImGui::Button("Respawn"))
			deferred = [o]
			{
				if (auto *f = o->get_component<FlockComponent>())
					f->respawn();
			};
		ImGui::SameLine();
		ImGui::TextDisabled("rebuild w/ count, mesh & physics");
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragInt("Count", &fl->count, 1.0f, 0, 2000);
		tip("How many boids the flock spawns. Press Respawn to apply.");

		constexpr auto open = ImGuiTreeNodeFlags_DefaultOpen;

		if (ImGui::CollapsingHeader("Flocking rules", open))
		{
			ImGui::DragFloat("Separation", &fl->w_separation, 0.02f, 0.0f, 10.0f);
			tip("SEPARATION: steer away from flockmates that are too close (within\n"
			    "'Separation dist'), so they don't clump or overlap. Higher = more spacing.");
			ImGui::DragFloat("Alignment", &fl->w_alignment, 0.02f, 0.0f, 10.0f);
			tip("ALIGNMENT: match the average heading/velocity of nearby boids, so the\n"
			    "flock moves as one. Higher = more orderly, parallel motion.");
			ImGui::DragFloat("Cohesion", &fl->w_cohesion, 0.02f, 0.0f, 10.0f);
			tip("COHESION: steer toward the average position of nearby boids, keeping the\n"
			    "group together. Higher = tighter flock; too high fights Separation.");
			ImGui::DragFloat("Perception", &fl->perception, 0.05f, 0.2f, 20.0f);
			tip("Neighbour radius: only boids within this distance count for Alignment\n"
			    "and Cohesion. Larger = each boid 'sees' more of the flock.");
			ImGui::DragFloat("Separation dist", &fl->separation_dist, 0.02f, 0.05f, 10.0f);
			tip("Boids closer than this trigger the Separation push -- their personal space.");
		}

		if (ImGui::CollapsingHeader("Movement & bounds", open))
		{
			ImGui::DragFloat("Max speed", &fl->max_speed, 0.1f, 0.1f, 40.0f);
			tip("Top travel speed. For physics boids only the HORIZONTAL speed is capped\n"
			    "here (gravity/buoyancy drive the vertical axis).");
			ImGui::DragFloat("Min speed", &fl->min_speed, 0.1f, 0.0f, 40.0f);
			tip("Lowest speed a boid keeps, so it never stalls in place.");
			ImGui::DragFloat("Max force", &fl->max_force, 0.1f, 0.1f, 60.0f);
			tip("Steering acceleration limit: how sharply a boid can change course.\n"
			    "Lower = lazy, wide turns; higher = snappy, darting turns.");
			ImGui::DragFloat("Bounds pull", &fl->w_bounds, 0.02f, 0.0f, 20.0f);
			tip("How strongly boids are pushed back inside the Bounds box. 0 = no box.");
			ImGui::DragFloat3("Bounds (half)", &fl->bounds.x, 0.1f, 0.5f, 60.0f);
			tip("Half-size of the box the flock is kept inside, centred on this object.");
			ImGui::DragFloat("Spawn radius", &fl->spawn_radius, 0.1f, 0.0f, 30.0f);
			tip("Radius of the sphere boids are scattered in when (re)spawned.");
		}

		if (ImGui::CollapsingHeader("Obstacle avoidance"))
		{
			ImGui::DragFloat("Avoid obstacles", &fl->w_avoid, 0.02f, 0.0f, 12.0f);
			tip("How strongly boids steer away from scene colliders (floor, walls, props).\n"
			    "0 = ignore obstacles. Works in both modes; physics boids also bump them.");
			ImGui::DragFloat("Avoid radius", &fl->avoid_radius, 0.05f, 0.1f, 12.0f);
			tip("How far ahead obstacles start to be felt for avoidance.");
		}

		if (ImGui::CollapsingHeader("Target & shape"))
		{
			const std::string cur = fl->target_name.empty() ? "<none>" : fl->target_name;
			if (ImGui::BeginCombo("Follow target", cur.c_str()))
			{
				if (ImGui::Selectable("<none>", fl->target_name.empty()))
					fl->target_name.clear();
				for (const auto &up : ObjectsManager::instance().objects())
				{
					if (up->get_component<BoidComponent>())
						continue; // don't list the boids
					if (up.get() == o)
						continue; // nor the flock itself
					ImGui::PushID(up.get());
					if (ImGui::Selectable(up->name().c_str(), fl->target_name == up->name()))
						fl->target_name = up->name();
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			tip("An Object the flock relates to. 'Form shape' off = follow/chase it;\n"
			    "on = take its mesh shape. Move it with the gizmo and the flock reacts.");
			ImGui::DragFloat("Follow strength", &fl->w_seek, 0.02f, 0.0f, 8.0f);
			tip("How hard boids chase the target when just following (Form shape off).\n"
			    "Raise it (and lower Bounds pull) to chase across the scene.");
			ImGui::Checkbox("Form target's shape", &fl->form_shape);
			tip("Boids settle onto points sampled over the target MODEL's surface, so the\n"
			    "flock 'draws' its shape. Best with Physics OFF. Move the model -> shape follows.");
			ImGui::BeginDisabled(!fl->form_shape);
			ImGui::DragFloat("Form strength", &fl->w_form, 0.05f, 0.0f, 20.0f);
			tip("How tightly boids snap to their slot on the model. Higher = crisper shape.");
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Appearance & physics", open))
		{
			ImGui::TextDisabled("changes below need Respawn");
			if (ImGui::BeginCombo("Boid mesh", fl->boid_mesh.c_str()))
			{
				for (const std::string &mn : AssetManager::instance().mesh_names())
				{
					ImGui::PushID(mn.c_str());
					if (ImGui::Selectable(mn.c_str(), fl->boid_mesh == mn))
						fl->boid_mesh = mn;
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			tip("The mesh every boid uses. Press Respawn to apply. Pyramid shows heading;\n"
			    "Sphere/Cube/etc. are uniform.");
			ImGui::DragFloat("Boid scale", &fl->boid_scale, 0.005f, 0.02f, 3.0f);
			tip("Size of each boid.");
			ImGui::ColorEdit3("Boid color", &fl->boid_color.x);
			tip("Albedo colour of the boids.");
			ImGui::Checkbox("Physics (Rigidbody)", &fl->physics);
			tip("OFF: pure boids, moved directly (fast, no gravity/collisions).\n"
			    "ON: each boid gets a Rigidbody + Collider and is force-steered, so gravity,\n"
			    "collisions and Buoyancy all affect it.");
			ImGui::BeginDisabled(!fl->physics);
			ImGui::Checkbox("Gravity", &fl->gravity);
			tip("Physics boids fall under gravity (e.g. a school settling onto water).");
			ImGui::Checkbox("Buoyancy", &fl->buoyancy);
			tip("Adds a BuoyancyComponent to each boid so they float on the scene's water.");
			ImGui::EndDisabled();
		}
		if (rm)
			deferred = [o] { o->remove_component<FlockComponent>(); };
	}

	if (auto *cloth = o->get_component<ClothComponent>())
	{
		bool rm = false;
		component_header("ClothComponent", rm);
		bool changed = false;
		changed |= ImGui::SliderInt("Columns", &cloth->cols, 2, 80);
		changed |= ImGui::SliderInt("Rows", &cloth->rows, 2, 80);
		changed |= ImGui::DragFloat("Spacing", &cloth->spacing, 0.005f, 0.02f, 1.0f);
		const char *pins[] = {"Top corners", "Top row", "Corners + centre", "Four corners"};
		changed |= ImGui::Combo("Pinned", &cloth->pin_mode, pins, IM_ARRAYSIZE(pins));
		changed |= ImGui::SliderFloat("Slack", &cloth->slack, 0.0f, 0.6f); // deeper drape
		ImGui::DragFloat("Stiffness", &cloth->stiffness, 10.0f, 50.0f, 8000.0f, "%.0f");
		ImGui::DragFloat("Bend (drape)", &cloth->bend_stiffness, 1.0f, 0.0f, 800.0f, "%.0f");
		ImGui::DragFloat("Spring damp", &cloth->spring_damping, 0.05f, 0.0f, 20.0f);
		ImGui::DragFloat("Mass/node", &cloth->mass, 0.002f, 0.005f, 1.0f, "%.3f");
		ImGui::SliderInt("Solver iters", &cloth->iterations, 5, 60); // CG iterations
		ImGui::DragFloat("Gravity scale", &cloth->gravity_scale, 0.02f, 0.0f, 4.0f);
		ImGui::DragFloat("Air drag", &cloth->damping, 0.0005f, 0.95f, 1.0f, "%.3f");
		ImGui::Checkbox("Tearable", &cloth->tearable);
		if (cloth->tearable)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120.0f);
			ImGui::DragFloat("Tear at x", &cloth->tear_factor, 0.02f, 1.1f, 5.0f, "%.2f");
		}
		if (changed)
			cloth->mark_dirty(); // rebuild the grid next frame
		if (ImGui::Button("Reset"))
			cloth->reset_sim();
		ImGui::TextDisabled("Ctrl + drag in the scene to grab/pull a node.");
		ImGui::TextDisabled("Hangs from pinned points (object pos = anchor).");
		if (rm)
			deferred = [o] { o->remove_component<ClothComponent>(); };
	}

	if (auto *mv = o->get_component<MoverComponent>())
	{
		bool rm = false;
		component_header("MoverComponent", rm);
		ImGui::Checkbox("Enabled", &mv->enabled);
		ImGui::SameLine();
		ImGui::Checkbox("Smooth", &mv->smooth);
		ImGui::DragFloat3("Axis", &mv->axis.x, 0.01f, -1.0f, 1.0f);
		ImGui::DragFloat("Distance", &mv->distance, 0.05f, 0.0f, 100.0f);
		ImGui::DragFloat("Speed", &mv->speed, 0.05f, 0.0f, 50.0f);
		if (ImGui::Button("Set start = current pos"))
			mv->recapture();
		ImGui::TextDisabled("Shuttles back and forth along axis from its start.");
		if (rm)
			deferred = [o] { o->remove_component<MoverComponent>(); };
	}

	if (auto *pm = o->get_component<PhysicsMoverComponent>())
	{
		bool rm = false;
		component_header("PhysicsMoverComponent", rm);
		ImGui::Checkbox("Enabled", &pm->enabled);
		ImGui::SameLine();
		ImGui::Checkbox("Circular (orbit)", &pm->circular);
		if (!pm->circular)
		{
			ImGui::SameLine();
			ImGui::Checkbox("Smooth", &pm->smooth);
		}
		ImGui::DragFloat3(pm->circular ? "Orbit axis" : "Axis", &pm->axis.x, 0.01f, -1.0f, 1.0f);
		ImGui::DragFloat(pm->circular ? "Radius" : "Distance", &pm->distance, 0.05f, 0.0f, 100.0f);
		ImGui::DragFloat("Speed", &pm->speed, 0.05f, 0.0f, 50.0f);
		if (ImGui::Button("Set start = current pos"))
			pm->recapture();
		ImGui::TextDisabled(pm->circular ? "Orbits its start point; pushes rigidbodies it hits."
		                                 : "Kinematic mover: pushes dynamic rigidbodies it hits.");
		if (rm)
			deferred = [o] { o->remove_component<PhysicsMoverComponent>(); };
	}

	if (auto *rb = o->get_component<RigidbodyComponent>())
	{
		bool rm = false;
		component_header("RigidbodyComponent", rm);
		ImGui::Checkbox("Use gravity", &rb->use_gravity);
		ImGui::SameLine();
		ImGui::Checkbox("Static", &rb->is_static);
		ImGui::Checkbox("Freeze rotation", &rb->freeze_rotation);
		ImGui::DragFloat("Mass", &rb->mass, 0.05f, 0.0f, 100.0f);
		ImGui::DragFloat("Restitution", &rb->restitution, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Friction", &rb->friction, 0.01f, 0.0f, 2.0f);
		ImGui::Text("Velocity:  %.2f, %.2f, %.2f", rb->velocity.x, rb->velocity.y, rb->velocity.z);
		ImGui::Text("Angular:   %.2f, %.2f, %.2f", rb->angular_velocity.x, rb->angular_velocity.y,
		            rb->angular_velocity.z);
		if (rm)
			deferred = [o] { o->remove_component<RigidbodyComponent>(); };
	}

	if (auto *col = o->get_component<ColliderComponent>())
	{
		bool rm = false;
		component_header("ColliderComponent", rm);
		if (col->shape == ColliderComponent::Shape::Heightfield)
		{
			ImGui::TextDisabled("Heightfield (terrain surface).");
			ImGui::TextDisabled("Bodies rest on the terrain; toggle it on the Terrain component.");
		}
		else
		{
			const char *shapes[] = {"Box", "Sphere", "Plane"};
			int sh = static_cast<int>(col->shape);
			if (ImGui::Combo("Shape", &sh, shapes, 3))
				col->shape = static_cast<ColliderComponent::Shape>(sh);
			if (col->shape == ColliderComponent::Shape::Sphere)
				ImGui::DragFloat("Radius", &col->radius, 0.01f, 0.01f, 50.0f);
			else if (col->shape == ColliderComponent::Shape::Box)
				ImGui::DragFloat3("Half extents", &col->half_extents.x, 0.01f, 0.01f, 50.0f);
			else
				ImGui::DragFloat3("Plane normal", &col->plane_normal.x, 0.01f, -1.0f, 1.0f);
			if (col->shape != ColliderComponent::Shape::Plane)
				ImGui::DragFloat3("Center offset", &col->center.x, 0.01f, -50.0f, 50.0f);
			if (ImGui::Button("Fit to mesh"))
				col->fit_to_mesh();
			ImGui::SameLine();
			ImGui::TextDisabled("(auto-size to the model's bounds)");
		}
		if (rm)
			deferred = [o] { o->remove_component<ColliderComponent>(); };
	}

	if (auto *c = o->get_component<CameraComponent>())
	{
		bool rm = false;
		component_header("CameraComponent", rm);
		ImGui::DragFloat("FOV", &c->fov_degrees, 0.5f, 10.0f, 120.0f);
		ImGui::DragFloat("Near", &c->near_plane, 0.01f, 0.01f, 10.0f);
		ImGui::DragFloat("Far", &c->far_plane, 1.0f, 10.0f, 1000.0f);
		if (rm)
			deferred = [o] { o->remove_component<CameraComponent>(); };
	}

	if (auto *f = o->get_component<FlyCameraController>())
	{
		bool rm = false;
		component_header("FlyCameraController", rm);
		ImGui::DragFloat("Move speed", &f->move_speed, 0.1f, 0.1f, 50.0f);
		ImGui::DragFloat("Sensitivity", &f->sensitivity, 0.01f, 0.01f, 2.0f);
		if (rm)
			deferred = [o] { o->remove_component<FlyCameraController>(); };
	}

	ImGui::Spacing();
	if (ImGui::Button("Add Component", ImVec2(-1, 0)))
		ImGui::OpenPopup("add_component");
	if (ImGui::BeginPopup("add_component"))
	{
		bool any = false;
		for (const ComponentType &ct : component_registry())
			if (!ct.has(*o))
			{
				any = true;
				if (ImGui::MenuItem(ct.name))
					ct.add(*o);
			}
		if (!any)
			ImGui::TextDisabled("(all components present)");
		ImGui::EndPopup();
	}

	if (deferred)
		deferred();
}

} // namespace

void draw_debug_ui()
{
	ObjectsManager &objects = ObjectsManager::instance();
	RendererManager &renderer = RendererManager::instance();
	const ImGuiIO &io = ImGui::GetIO();

	// Dockspace over the viewport, leaving the 3D scene visible in the center.
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const ImGuiID dock_id =
	    ImGui::DockSpaceOverViewport(0, vp, ImGuiDockNodeFlags_PassthruCentralNode);

	// Build the default layout once: Hierarchy top-left, Stats/Scenes
	// bottom-left, Inspector right, scene in the center.
	static bool layout_built = false;
	if (!layout_built)
	{
		layout_built = true;
		ImGui::DockBuilderRemoveNode(dock_id);
		ImGui::DockBuilderAddNode(dock_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dock_id, vp->Size);

		ImGuiID center = dock_id;
		const ImGuiID left_id =
		    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
		const ImGuiID right_id =
		    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
		ImGuiID left_bottom = 0;
		const ImGuiID left_top =
		    ImGui::DockBuilderSplitNode(left_id, ImGuiDir_Up, 0.6f, nullptr, &left_bottom);

		ImGui::DockBuilderDockWindow("Hierarchy", left_top);
		ImGui::DockBuilderDockWindow("Stats", left_bottom);
		ImGui::DockBuilderDockWindow("Scenes", left_bottom);
		ImGui::DockBuilderDockWindow("Prefabs", left_bottom);
		ImGui::DockBuilderDockWindow("Inspector", right_id);
		ImGui::DockBuilderFinish(dock_id);
	}

	draw_main_menu_bar();

	// Keyboard shortcuts (unless typing in a field).
	if (!io.WantTextInput)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Delete) && objects.selected())
			objects.remove_object(objects.selected());
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && objects.selected())
			duplicate_object(objects.selected());
		if (ImGui::IsKeyPressed(ImGuiKey_F) && objects.selected())
			focus_selected(objects.selected());
		if (ImGui::IsKeyPressed(ImGuiKey_1))
			g_gizmo_op = ImGuizmo::TRANSLATE;
		if (ImGui::IsKeyPressed(ImGuiKey_2))
			g_gizmo_op = ImGuizmo::ROTATE;
		if (ImGui::IsKeyPressed(ImGuiKey_3))
			g_gizmo_op = ImGuizmo::SCALE;
	}

	// --- Stats / Renderer ---------------------------------------------------
	if (ImGui::Begin("Stats"))
	{
		ImGui::Text("%.1f FPS  (%.2f ms/frame)", io.Framerate, 1000.0f / io.Framerate);

		// Per-pass GPU/CPU breakdown (timer queries, ~2 frames of latency). The
		// totals tell us whether a scene is GPU- or CPU(render-thread)-bound, and
		// which pass to attack first.
		const RendererManager::StageTime *st = renderer.stage_times();
		double gpu_total = 0.0, cpu_total = 0.0;
		for (int s = 0; s < RendererManager::PROF_COUNT; ++s)
		{
			gpu_total += st[s].gpu_ms;
			cpu_total += st[s].cpu_ms;
		}
		ImGui::Text("GPU %.2f ms   CPU(render) %.2f ms", gpu_total, cpu_total);
		if (ImGui::BeginTable("prof", 3,
		                      ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("Pass");
			ImGui::TableSetupColumn("GPU ms");
			ImGui::TableSetupColumn("CPU ms");
			ImGui::TableHeadersRow();
			for (int s = 0; s < RendererManager::PROF_COUNT; ++s)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(RendererManager::stage_name(s));
				ImGui::TableNextColumn();
				ImGui::Text("%.2f", st[s].gpu_ms);
				ImGui::TableNextColumn();
				ImGui::Text("%.2f", st[s].cpu_ms);
			}
			ImGui::EndTable();
		}
		ImGui::Separator();
		ImGui::ColorEdit3("Clear color", &renderer.clear_color.x);
		ImGui::SeparatorText("Shadows");
		ImGui::DragFloat("Strength", &renderer.shadow_strength, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Area", &renderer.shadow_ortho_size, 0.25f, 2.0f, 60.0f);
		ImGui::Checkbox("Auto-skip when static", &renderer.shadows_auto_skip);
		ImGui::SameLine();
		ImGui::TextDisabled(renderer.shadows_updated_last() ? "(rendering)" : "(reusing)");

		ImGui::SeparatorText("Post-processing");
		auto &post = renderer.post;
		ImGui::Checkbox("Enabled", &post.enabled);
		ImGui::BeginDisabled(!post.enabled);
		ImGui::DragFloat("Exposure", &post.exposure, 0.01f, 0.1f, 4.0f);
		ImGui::DragFloat("Vignette", &post.vignette, 0.01f, 0.0f, 1.0f);
		ImGui::Checkbox("FXAA", &post.fxaa);
		const char *msaa_labels[] = {"Off (1x)", "2x", "4x"};
		const int msaa_vals[] = {1, 2, 4};
		int msaa_idx = (post.msaa >= 4) ? 2 : (post.msaa >= 2 ? 1 : 0);
		if (ImGui::Combo("MSAA", &msaa_idx, msaa_labels, 3))
			post.msaa = msaa_vals[msaa_idx];
		ImGui::Checkbox("Depth of field", &post.dof);
		ImGui::BeginDisabled(!post.dof);
		ImGui::DragFloat("DoF focus", &post.dof_focus, 0.1f, 0.5f, 100.0f);
		ImGui::DragFloat("DoF range", &post.dof_range, 0.1f, 0.5f, 100.0f);
		ImGui::DragFloat("DoF blur", &post.dof_radius, 0.1f, 0.0f, 24.0f);
		ImGui::EndDisabled();
		ImGui::Checkbox("SSR (reflections)", &post.ssr);
		ImGui::BeginDisabled(!post.ssr);
		ImGui::DragFloat("SSR intensity", &post.ssr_intensity, 0.01f, 0.0f, 2.0f);
		ImGui::DragInt("SSR steps", &post.ssr_steps, 1.0f, 4, 128);
		ImGui::EndDisabled();
		ImGui::Checkbox("Fog", &post.fog);
		ImGui::BeginDisabled(!post.fog);
		ImGui::ColorEdit3("Fog color", &post.fog_color.x);
		ImGui::DragFloat("Fog density", &post.fog_density, 0.001f, 0.0f, 0.5f, "%.3f");
		ImGui::EndDisabled();
		ImGui::Checkbox("Bloom", &post.bloom);
		ImGui::BeginDisabled(!post.bloom);
		ImGui::DragFloat("Bloom threshold", &post.bloom_threshold, 0.01f, 0.0f, 5.0f);
		ImGui::DragFloat("Bloom intensity", &post.bloom_intensity, 0.01f, 0.0f, 3.0f);
		ImGui::EndDisabled();
		ImGui::Checkbox("SSAO", &post.ssao);
		ImGui::BeginDisabled(!post.ssao);
		ImGui::DragFloat("AO radius", &post.ssao_radius, 0.01f, 0.05f, 3.0f);
		ImGui::DragFloat("AO strength", &post.ssao_strength, 0.01f, 0.0f, 4.0f);
		ImGui::DragFloat("AO bias", &post.ssao_bias, 0.001f, 0.0f, 0.2f);
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		ImGui::SeparatorText("Physics");
		PhysicsManager &physics = PhysicsManager::instance();
		ImGui::Checkbox("Simulate", &physics.enabled);
		ImGui::SameLine();
		if (ImGui::Button("Reset"))
			physics.reset();
		ImGui::DragFloat3("Gravity", &physics.gravity.x, 0.1f);
		ImGui::Checkbox("Show colliders (wireframe)", &renderer.show_colliders);

		CameraComponent *cam = renderer.main_camera();
		ImGui::Text("Main camera: %s", cam ? cam->owner()->name().c_str() : "<none>");
	}
	ImGui::End();

	// --- Hierarchy (tree with drag-drop parenting) --------------------------
	if (ImGui::Begin("Hierarchy"))
	{
		if (ImGui::SmallButton("+ Cube"))
			spawn_primitive("Cube");
		ImGui::SameLine();
		if (ImGui::SmallButton("+ Sphere"))
			spawn_primitive("Sphere");
		ImGui::SameLine();
		if (ImGui::SmallButton("Delete") && objects.selected())
			objects.remove_object(objects.selected());
		ImGui::Separator();

		Object *sel = objects.selected();
		// Re-parenting is deferred until after the tree is drawn so we never mutate
		// the hierarchy mid-traversal. {child, newParent}; newParent == nullptr = root.
		Object *drag_child = nullptr;
		Object *drag_new_parent = nullptr;
		bool did_drop = false;

		std::function<void(Object *)> draw_node = [&](Object *o)
		{
			ImGui::PushID(o);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
			                           ImGuiTreeNodeFlags_SpanAvailWidth |
			                           ImGuiTreeNodeFlags_DefaultOpen;
			if (o->children().empty())
				flags |= ImGuiTreeNodeFlags_Leaf;
			if (sel == o)
				flags |= ImGuiTreeNodeFlags_Selected;

			const bool open = ImGui::TreeNodeEx(o->name().c_str(), flags);
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
				objects.set_selected(o);
			// Drag this object...
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("CF_OBJ", &o, sizeof(Object *));
				ImGui::Text("%s", o->name().c_str());
				ImGui::EndDragDropSource();
			}
			// ...drop it onto another to make it that object's child.
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("CF_OBJ"))
				{
					drag_child = *static_cast<Object *const *>(pl->Data);
					drag_new_parent = o;
					did_drop = true;
				}
				ImGui::EndDragDropTarget();
			}
			if (open)
			{
				for (Object *c : o->children())
					draw_node(c);
				ImGui::TreePop();
			}
			ImGui::PopID();
		};

		for (const auto &up : objects.objects())
			if (!up->parent())
				draw_node(up.get());

		// Drop onto the empty area below to un-parent back to the scene root.
		const float drop_h = std::max(24.0f, ImGui::GetContentRegionAvail().y);
		ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, drop_h));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("CF_OBJ"))
			{
				drag_child = *static_cast<Object *const *>(pl->Data);
				drag_new_parent = nullptr;
				did_drop = true;
			}
			ImGui::EndDragDropTarget();
		}
		if (did_drop && drag_child)
			drag_child->set_parent(drag_new_parent); // keepWorld: object stays put
	}
	ImGui::End();

	// --- Inspector ----------------------------------------------------------
	if (ImGui::Begin("Inspector"))
	{
		if (Object *o = objects.selected())
			draw_inspector(o);
		else
			ImGui::TextDisabled("Select an object (click it, or use the Hierarchy).");
	}
	ImGui::End();

	// --- Scenes (save / load / new) -----------------------------------------
	// Deferred so we don't clear objects mid-frame while other panels read them.
	std::function<void()> scene_action;
	if (ImGui::Begin("Scenes"))
	{
		static char name_buf[64] = "scene";
		ImGui::InputText("Name", name_buf, sizeof(name_buf));
		if (ImGui::Button("Save"))
		{
			const std::string n = name_buf;
			scene_action = [n] { Scene::save(Scene::file_path(n)); };
		}
		ImGui::SameLine();
		if (ImGui::Button("New"))
			scene_action = [] { new_minimal_scene(); };
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
			scene_action = [] { ObjectsManager::instance().clear(); };
		ImGui::SeparatorText("Load");
		for (const std::string &s : Scene::list())
		{
			if (ImGui::Selectable(s.c_str()))
				scene_action = [s] { Scene::load(Scene::file_path(s)); };
			// Right-click a scene -> delete it (with a confirm so it's not by accident).
			if (ImGui::BeginPopupContextItem(s.c_str()))
			{
				ImGui::TextDisabled("%s", s.c_str());
				if (ImGui::MenuItem("Delete scene"))
					scene_action = [s] { Scene::remove(Scene::file_path(s)); };
				ImGui::EndPopup();
			}
		}
		ImGui::TextDisabled("Right-click a scene to delete it.");
	}
	ImGui::End();

	// --- Prefabs (save selected object as a template / spawn copies) ---------
	if (ImGui::Begin("Prefabs"))
	{
		static char prefab_name[64] = "prefab";
		static Object *last_sel = nullptr;
		Object *sel = objects.selected();
		if (sel && sel != last_sel)
		{ // prefill the name from the newly selected object
			const std::size_t k = sel->name().copy(prefab_name, sizeof(prefab_name) - 1);
			prefab_name[k] = '\0';
			last_sel = sel;
		}
		ImGui::InputText("Name##prefab", prefab_name, sizeof(prefab_name));
		ImGui::BeginDisabled(sel == nullptr || prefab_name[0] == '\0');
		if (ImGui::Button("Save selected"))
		{
			const std::string n = prefab_name;
			scene_action = [sel, n] { Prefab::save(sel, n); };
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (sel)
			ImGui::TextDisabled("from \"%s\"", sel->name().c_str());
		else
			ImGui::TextDisabled("(select an object)");
		ImGui::SeparatorText("Spawn");
		for (const std::string &p : Prefab::list())
		{
			if (ImGui::Selectable(p.c_str()))
				scene_action = [p] { Prefab::instantiate(Prefab::file_path(p)); };
			if (ImGui::BeginPopupContextItem(p.c_str()))
			{
				ImGui::TextDisabled("%s", p.c_str());
				if (ImGui::MenuItem("Delete prefab"))
					scene_action = [p] { Prefab::remove(Prefab::file_path(p)); };
				ImGui::EndPopup();
			}
		}
		ImGui::TextDisabled("Click to spawn into the scene. Right-click to delete.");
	}
	ImGui::End();

	// Transform gizmo over the scene for the selected object.
	if (Object *o = objects.selected())
		draw_gizmo(o);

	if (scene_action)
		scene_action();
}

void draw_stats_overlay()
{
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float pad = 12.0f;
	const ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - pad, vp->WorkPos.y + pad);
	ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f)); // anchor top-right
	ImGui::SetNextWindowBgAlpha(0.45f);
	const ImGuiWindowFlags flags =
	    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
	if (ImGui::Begin("##stats_overlay", nullptr, flags))
	{
		const ImGuiIO &io = ImGui::GetIO();
		RendererManager &r = RendererManager::instance();
		ImGui::Text("%.0f FPS  (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
		const RendererManager::StageTime *st = r.stage_times();
		double gpu = 0.0, cpu = 0.0;
		for (int s = 0; s < RendererManager::PROF_COUNT; ++s)
		{
			gpu += st[s].gpu_ms;
			cpu += st[s].cpu_ms;
		}
		ImGui::Text("GPU %.2f   CPU %.2f ms", gpu, cpu);
		ImGui::Separator();
		for (int s = 0; s < RendererManager::PROF_COUNT; ++s)
			ImGui::Text("%-9s %5.2f ms", RendererManager::stage_name(s), st[s].gpu_ms);
	}
	ImGui::End();
}

} // namespace cf
