#include "ui/UIManager.h"

#include "core/WindowManager.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <ImGuizmo.h>

namespace cf
{

UIManager &UIManager::instance()
{
	static UIManager s;
	return s;
}

void UIManager::init()
{
	if (m_initialized)
		return;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // dockable panels
	io.IniFilename = nullptr;                         // don't write imgui.ini

	ImGui::StyleColorsDark();

	GLFWwindow *win = WindowManager::instance().handle();
	ImGui_ImplGlfw_InitForOpenGL(win, true); // true = install + chain callbacks
	ImGui_ImplOpenGL3_Init("#version 460");

	m_initialized = true;
}

void UIManager::begin_frame()
{
	if (!m_initialized)
		return;
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
}

void UIManager::end_frame()
{
	if (!m_initialized)
		return;
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool UIManager::wants_mouse() const
{
	return m_initialized && ImGui::GetIO().WantCaptureMouse;
}

bool UIManager::wants_keyboard() const
{
	return m_initialized && ImGui::GetIO().WantCaptureKeyboard;
}

bool UIManager::gizmo_active() const
{
	return m_initialized && (ImGuizmo::IsUsing() || ImGuizmo::IsOver());
}

void UIManager::shutdown()
{
	if (!m_initialized)
		return;
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	m_initialized = false;
}

UIManager::~UIManager()
{
	// Real teardown happens in shutdown() while the GL context is still alive.
}

} // namespace cf
