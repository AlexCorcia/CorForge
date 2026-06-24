#pragma once

namespace cf
{

// Builds the CorForge debug/editor window. Call between
// UIManager::beginFrame() and UIManager::endFrame(). Reads/writes the
// singleton managers directly.
void draw_debug_ui();

} // namespace cf
