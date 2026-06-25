#pragma once

namespace cf
{

// Builds the CorForge debug/editor window. Call between
// UIManager::beginFrame() and UIManager::endFrame(). Reads/writes the
// singleton managers directly.
void draw_debug_ui();

// Compact always-on perf overlay pinned to the top-right corner (FPS + per-pass
// GPU/CPU ms). Click-through (no inputs); shown in both editor and cinematic mode.
void draw_stats_overlay();

} // namespace cf
