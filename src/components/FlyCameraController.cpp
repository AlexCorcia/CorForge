#include "components/FlyCameraController.h"

#include "core/Object.h"
#include "core/WindowManager.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>

namespace cf
{

void FlyCameraController::update(float dt)
{
	WindowManager &win = WindowManager::instance();
	Transform &t = owner()->transform;

	// Don't steal input while the user is interacting with the ImGui UI.
	const ImGuiIO &io = ImGui::GetIO();

	// --- Mouse look (only while right mouse button is held) ----------------
	const bool looking = !io.WantCaptureMouse && win.mouse_button_pressed(GLFW_MOUSE_BUTTON_RIGHT);
	win.set_cursor_captured(looking);

	double mx = 0.0, my = 0.0;
	win.cursor_pos(mx, my);

	if (looking)
	{
		if (m_first_mouse)
		{ // avoid a jump on the first frame of looking
			m_last_x = mx;
			m_last_y = my;
			m_first_mouse = false;
		}
		const float dx = static_cast<float>(mx - m_last_x);
		const float dy = static_cast<float>(my - m_last_y);
		m_last_x = mx;
		m_last_y = my;

		t.euler_degrees.y += dx * sensitivity; // yaw
		t.euler_degrees.x -= dy * sensitivity; // pitch (screen Y is flipped)
		t.euler_degrees.x = std::clamp(t.euler_degrees.x, -89.0f, 89.0f);
	}
	else
	{
		m_first_mouse = true;
	}

	// --- Movement ----------------------------------------------------------
	if (io.WantCaptureKeyboard)
		return; // typing into an ImGui field; don't move

	float speed = move_speed * dt;
	if (win.key_pressed(GLFW_KEY_LEFT_SHIFT))
		speed *= sprint_mul;

	const glm::vec3 fwd = t.forward();
	const glm::vec3 right = t.right();
	const glm::vec3 world_up{0.0f, 1.0f, 0.0f};

	if (win.key_pressed(GLFW_KEY_W))
		t.position += fwd * speed;
	if (win.key_pressed(GLFW_KEY_S))
		t.position -= fwd * speed;
	if (win.key_pressed(GLFW_KEY_D))
		t.position += right * speed;
	if (win.key_pressed(GLFW_KEY_A))
		t.position -= right * speed;
	if (win.key_pressed(GLFW_KEY_E))
		t.position += world_up * speed;
	if (win.key_pressed(GLFW_KEY_Q))
		t.position -= world_up * speed;
}

} // namespace cf
