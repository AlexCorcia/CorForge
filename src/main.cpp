#include "components/CameraComponent.h"
#include "components/ClothComponent.h"
#include "core/AssetManager.h"
#include "core/Object.h"
#include "core/ObjectsManager.h"
#include "core/PhysicsManager.h"
#include "core/RendererManager.h"
#include "core/SceneSerializer.h"
#include "core/WindowManager.h"
#include "gfx/Shader.h"
#include "ui/DebugUI.h"
#include "ui/UIManager.h"

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>

using namespace cf;

int main()
try
{
	// --- Bring up the singletons -------------------------------------------
	WindowManager::instance().init(1280, 720, "CorForge");
	RendererManager::instance().init();
	UIManager::instance().init();
	AssetManager::instance().init();
	ObjectsManager &objects = ObjectsManager::instance();

	// Engine defaults (used by the editor and by materials in loaded scenes).
	auto shader = std::make_shared<Shader>(CORFORGE_SHADER_DIR "/basic.vert",
	                                       CORFORGE_SHADER_DIR "/basic.frag");
	RendererManager::instance().set_default_shader(shader);
	RendererManager::instance().set_default_mesh(AssetManager::instance().mesh("Cube"));

	// Start from a saved scene rather than building one in code. The startup scene
	// can be overridden with CORFORGE_SCENE (e.g. for profiling a specific scene).
	const char *start_scene = std::getenv("CORFORGE_SCENE");
	Scene::load(Scene::file_path(start_scene ? start_scene : "DonutDemo"));

	// --- Main loop ----------------------------------------------------------
	WindowManager &window = WindowManager::instance();
	float last = static_cast<float>(glfwGetTime());
	bool prev_left_mouse = false;
	bool prev_esc = false;
	ClothComponent *grabbed_cloth = nullptr; // cloth currently being dragged
	float grab_depth = 0.0f;                 // fixed camera distance of the grabbed node

	// --- Cinematic demo mode (F1) -------------------------------------------
	// Hides the editor UI and flies an auto-framed orbit camera around the scene,
	// cycling through the demo scenes. Lets the engine present itself hands-free.
	bool demo_mode = false;
	bool prev_f1 = false;
	bool show_overlay = true; // corner perf overlay (F2)
	bool prev_f2 = false;
	float demo_angle = 0.0f, demo_clock = 0.0f, scene_timer = 0.0f;
	float demo_zoom = 1.0f; // cinematic zoom (scroll wheel), multiplies orbit distance
	int demo_scene_idx = 0;
	const char *demo_scenes[] = {"DonutDemo",   "TerrainDemo",   "FlockingDemo",
	                             "ParticleDemo", "CoolPoolScene", "MirrorRoom"};
	const int demo_scene_count = 6;
	const float k_scene_seconds = 18.0f; // time on each scene before cycling

	while (!window.should_close())
	{
		window.poll_events();
		// Esc: exit cinematic mode first, then clear selection, then quit.
		const bool esc = window.key_pressed(GLFW_KEY_ESCAPE);
		if (esc && !prev_esc)
		{
			if (demo_mode)
				demo_mode = false;
			else if (objects.selected())
				objects.set_selected(nullptr);
			else
				break;
		}
		prev_esc = esc;

		// F1 toggles the hands-free cinematic demo.
		const bool f1 = window.key_pressed(GLFW_KEY_F1);
		if (f1 && !prev_f1)
		{
			demo_mode = !demo_mode;
			scene_timer = 0.0f;
			demo_angle = 0.0f;
			demo_zoom = 1.0f;
		}
		prev_f1 = f1;

		// F2 toggles the corner perf overlay.
		const bool f2 = window.key_pressed(GLFW_KEY_F2);
		if (f2 && !prev_f2)
			show_overlay = !show_overlay;
		prev_f2 = f2;

		const float now = static_cast<float>(glfwGetTime());
		const float dt = now - last;
		last = now;

		// Cinematic scene cycling: advance to the next demo scene on a timer.
		if (demo_mode)
		{
			demo_clock += dt;
			scene_timer += dt;
			if (scene_timer >= k_scene_seconds)
			{
				scene_timer = 0.0f;
				demo_angle = 0.0f;
				demo_scene_idx = (demo_scene_idx + 1) % demo_scene_count;
				objects.set_selected(nullptr); // old objects are about to be freed
				grabbed_cloth = nullptr;
				Scene::load(Scene::file_path(demo_scenes[demo_scene_idx]));
			}
		}

		UIManager::instance().begin_frame();
		if (!demo_mode)
			draw_debug_ui(); // editor UI hidden during the cinematic demo
		if (show_overlay)
			draw_stats_overlay(); // compact corner HUD, shown in both modes

		const bool left_mouse = window.mouse_button_pressed(GLFW_MOUSE_BUTTON_LEFT);
		const bool ctrl =
		    window.key_pressed(GLFW_KEY_LEFT_CONTROL) || window.key_pressed(GLFW_KEY_RIGHT_CONTROL);

		// Ctrl + left-drag grabs the nearest node of the selected cloth and drags
		// it (pull hard to tear, if tearing is on). Holding Ctrl bypasses picking.
		ClothComponent *cloth_sel =
		    objects.selected() ? objects.selected()->get_component<ClothComponent>() : nullptr;
		if (!demo_mode && ctrl && left_mouse && cloth_sel && !UIManager::instance().wants_mouse())
		{
			double mx = 0.0, my = 0.0;
			window.cursor_pos(mx, my);
			glm::vec3 ro, rd;
			if (RendererManager::instance().screen_ray(mx, my, ro, rd))
			{
				if (!cloth_sel->is_grabbing())
				{
					glm::vec3 hit;
					if (cloth_sel->grab(ro, rd, 0.6f, hit))
					{
						grabbed_cloth = cloth_sel;
						grab_depth = glm::dot(hit - ro, rd); // freeze the drag plane depth
					}
				}
				if (cloth_sel->is_grabbing())
					cloth_sel->move_grab(ro + rd * grab_depth);
			}
		}
		else if (grabbed_cloth)
		{
			grabbed_cloth->release_grab();
			grabbed_cloth = nullptr;
		}

		// Plain left-click in the scene picks an object (ignored when over the UI,
		// dragging the gizmo, or grabbing the cloth with Ctrl).
		if (!demo_mode && left_mouse && !prev_left_mouse && !ctrl &&
		    !UIManager::instance().wants_mouse() && !UIManager::instance().gizmo_active())
		{
			double mx = 0.0, my = 0.0;
			window.cursor_pos(mx, my);
			objects.set_selected(RendererManager::instance().pick(mx, my));
		}
		prev_left_mouse = left_mouse;

		PhysicsManager::instance().step(dt); // integrate + resolve collisions
		objects.update_all(dt);              // reads ImGui WantCapture flags

		// Cinematic camera: override the main camera onto a slow auto-framed orbit
		// (after update_all so it wins over the fly controller). Framing adapts to
		// the loaded scene's bounding sphere.
		if (demo_mode)
		{
			CameraComponent *cam = RendererManager::instance().main_camera();
			glm::vec3 center;
			float radius = 0.0f;
			if (cam && RendererManager::instance().scene_bounds(center, radius))
			{
				// Scroll wheel zooms the cinematic in/out (scales orbit distance).
				const float wheel = ImGui::GetIO().MouseWheel;
				demo_zoom = glm::clamp(demo_zoom - wheel * 0.08f, 0.3f, 3.0f);
				demo_angle += dt * 0.18f; // slow orbit
				const float dist = (radius * 1.5f + 1.5f) * demo_zoom;
				const float height =
				    center.y + radius * 0.35f + std::sin(demo_clock * 0.35f) * radius * 0.1f;
				glm::vec3 pos = center + glm::vec3(std::cos(demo_angle) * dist, 0.0f,
				                                   std::sin(demo_angle) * dist);
				pos.y = height;
				const glm::vec3 dir = glm::normalize(center - pos);
				Transform &t = cam->owner()->transform;
				t.position = pos;
				t.euler_degrees.x = glm::degrees(std::asin(glm::clamp(dir.y, -1.0f, 1.0f)));
				t.euler_degrees.y = glm::degrees(std::atan2(dir.x, -dir.z));
				t.euler_degrees.z = 0.0f;
			}
		}

		RendererManager::instance().render_frame();
		UIManager::instance().end_frame(); // draw UI on top of the scene

		window.swap_buffers();
	}

	// Tear down (order matters: do this while the GL context is still alive).
	UIManager::instance().shutdown();
	objects.clear();
	return EXIT_SUCCESS;
}
catch (const std::exception &e)
{
	std::fprintf(stderr, "Fatal: %s\n", e.what());
	return EXIT_FAILURE;
}
