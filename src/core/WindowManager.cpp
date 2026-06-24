#include "core/WindowManager.h"

#include <cstdio>
#include <stdexcept>

namespace cf
{

static void glfw_error_callback(int code, const char *desc)
{
	std::fprintf(stderr, "[GLFW error %d] %s\n", code, desc);
}

WindowManager &WindowManager::instance()
{
	static WindowManager s;
	return s;
}

void WindowManager::init(int width, int height, const std::string &title)
{
	if (m_initialized)
		throw std::runtime_error("WindowManager::init called twice");

	m_width = width;
	m_height = height;

	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		throw std::runtime_error("Failed to initialize GLFW");

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_SAMPLES, 4);      // 4x MSAA
	glfwWindowHint(GLFW_STENCIL_BITS, 8); // stencil for the selection outline
#ifndef NDEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

	m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
	if (!m_window)
	{
		glfwTerminate();
		throw std::runtime_error("Failed to create GLFW window");
	}

	glfwMakeContextCurrent(m_window);
	glfwSetWindowUserPointer(m_window, this);
	glfwSetFramebufferSizeCallback(m_window, framebuffer_size_callback);

	if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)))
	{
		glfwDestroyWindow(m_window);
		glfwTerminate();
		throw std::runtime_error("Failed to load OpenGL via GLAD");
	}

	glfwSwapInterval(1); // vsync
	m_initialized = true;

	std::printf("OpenGL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
}

WindowManager::~WindowManager()
{
	if (m_window)
		glfwDestroyWindow(m_window);
	if (m_initialized)
		glfwTerminate();
}

bool WindowManager::should_close() const
{
	return glfwWindowShouldClose(m_window);
}
void WindowManager::poll_events() const
{
	glfwPollEvents();
}
void WindowManager::swap_buffers() const
{
	glfwSwapBuffers(m_window);
}

bool WindowManager::key_pressed(int key) const
{
	return glfwGetKey(m_window, key) == GLFW_PRESS;
}

bool WindowManager::mouse_button_pressed(int button) const
{
	return glfwGetMouseButton(m_window, button) == GLFW_PRESS;
}

void WindowManager::cursor_pos(double &x, double &y) const
{
	glfwGetCursorPos(m_window, &x, &y);
}

void WindowManager::set_cursor_captured(bool captured) const
{
	glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void WindowManager::framebuffer_size_callback(GLFWwindow *win, int w, int h)
{
	auto *self = static_cast<WindowManager *>(glfwGetWindowUserPointer(win));
	self->m_width = w;
	self->m_height = h;
	glViewport(0, 0, w, h);
}

} // namespace cf
