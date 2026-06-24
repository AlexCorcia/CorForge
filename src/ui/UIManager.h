#pragma once

namespace cf
{

// Singleton wrapping the Dear ImGui lifecycle + GLFW/OpenGL3 backends.
// Usage per frame:  beginFrame()  ->  (build your ImGui windows)  ->  endFrame()
class UIManager
{
public:
	static UIManager &instance();

	UIManager(const UIManager &) = delete;
	UIManager &operator=(const UIManager &) = delete;

	void init();        // after the GL context + window exist
	void begin_frame(); // start a new ImGui frame
	void end_frame();   // render ImGui draw data on top of the scene
	void shutdown();    // call before the GL context is destroyed

	// True when ImGui is using the mouse/keyboard (so the app shouldn't).
	bool wants_mouse() const;
	bool wants_keyboard() const;

	// True when a transform gizmo is hovered or being dragged.
	bool gizmo_active() const;

private:
	UIManager() = default;
	~UIManager();

	bool m_initialized = false;
};

} // namespace cf
