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

	// Start from a saved scene rather than building one in code.
	Scene::load(Scene::file_path("DonutDemo"));

	// --- Main loop ----------------------------------------------------------
	WindowManager &window = WindowManager::instance();
	float last = static_cast<float>(glfwGetTime());
	bool prev_left_mouse = false;
	bool prev_esc = false;
	ClothComponent *grabbed_cloth = nullptr; // cloth currently being dragged
	float grab_depth = 0.0f;                 // fixed camera distance of the grabbed node

	while (!window.should_close())
	{
		window.poll_events();
		// Esc: first clear the selection; only quit when nothing is selected.
		const bool esc = window.key_pressed(GLFW_KEY_ESCAPE);
		if (esc && !prev_esc)
		{
			if (objects.selected())
				objects.set_selected(nullptr);
			else
				break;
		}
		prev_esc = esc;

		const float now = static_cast<float>(glfwGetTime());
		const float dt = now - last;
		last = now;

		UIManager::instance().begin_frame();
		draw_debug_ui();

		const bool left_mouse = window.mouse_button_pressed(GLFW_MOUSE_BUTTON_LEFT);
		const bool ctrl =
		    window.key_pressed(GLFW_KEY_LEFT_CONTROL) || window.key_pressed(GLFW_KEY_RIGHT_CONTROL);

		// Ctrl + left-drag grabs the nearest node of the selected cloth and drags
		// it (pull hard to tear, if tearing is on). Holding Ctrl bypasses picking.
		ClothComponent *cloth_sel =
		    objects.selected() ? objects.selected()->get_component<ClothComponent>() : nullptr;
		if (ctrl && left_mouse && cloth_sel && !UIManager::instance().wants_mouse())
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
		if (left_mouse && !prev_left_mouse && !ctrl && !UIManager::instance().wants_mouse() &&
		    !UIManager::instance().gizmo_active())
		{
			double mx = 0.0, my = 0.0;
			window.cursor_pos(mx, my);
			objects.set_selected(RendererManager::instance().pick(mx, my));
		}
		prev_left_mouse = left_mouse;

		PhysicsManager::instance().step(dt); // integrate + resolve collisions
		objects.update_all(dt);              // reads ImGui WantCapture flags
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
