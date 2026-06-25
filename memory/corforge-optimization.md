---
name: corforge-optimization
description: CorForge engine optimization effort — plan, findings, and progress
metadata:
  type: project
---

User wants to optimize the CorForge renderer (started 2026-06-25). Chose to **measure first**.

**Done:** Added a per-pass GPU+CPU profiler (GL_TIME_ELAPSED timer queries, double-buffered to avoid stalls) in [RendererManager.cpp](../src/core/RendererManager.cpp) via a `run_stage` lambda; 4 stages (Shadows/Captures/Scene/Post). Surfaced as a table in the Stats panel ([DebugUI.cpp](../src/ui/DebugUI.cpp)). Public API: `RendererManager::stage_times()`, `stage_name()`, enum `PROF_*`.

**Identified hotspots (not yet fixed), highest leverage first:**
1. Per-draw uniform churn — `draw_submesh` in [RendererComponent.cpp](../src/components/RendererComponent.cpp) re-uploads view/proj/8 lights/shadow matrices/sky/fog for *every submesh every frame*, building `std::string`s per light (~80 allocs/submesh). Fix = per-frame **UBO** bound once. Biggest CPU win.
2. `Shader::location` hashes a `std::string` per `set_*` ([Shader.cpp](../src/gfx/Shader.cpp)).
3. `world_matrix()` walks parent chain, recomputed several times/object/frame + `transpose(inverse())` per draw → cache per frame.
4. No draw sorting by shader/material → redundant state changes.
5. GPU: 2560×1600 + 4× MSAA RGBA16F + full-res SSAO + 10-iter bloom + per-object reflection cubemaps. Add render-scale slider + quality toggles; dirty-flag static shadow maps.

**Measured (MirrorRoom @2560x1600):** GPU-bound, NOT CPU-bound. Per-pass GPU: Scene ~3.9, Captures ~2.5, Post ~2.0, Shadows ~1.9 ms; CPU(render) total only ~0.7 ms. So the UBO/uniform-churn refactor (hotspot #1) would NOT help — CPU isn't the limiter. Lesson: measuring first prevented wasted work.

**Shipped (verified):** Shadow-pass **dirty-flag** — `render_shadow_maps` fingerprints (FNV-1a) all occluder world matrices + mesh ptrs + transmittance (albedo/opacity) + light params + ortho size; skips the entire GPU shadow render when unchanged. Reuses last frame's shadow textures (view-independent). Toggle `RendererManager::shadows_auto_skip` + "(rendering)/(reusing)" indicator in Stats panel. Verified: ParticleDemo (static geometry) Shadows GPU 1.5ms -> **0.00ms**.
- MirrorRoom does NOT benefit: it has 5 Mover components (balls + patrol cube) animating every frame, so shadows correctly re-render. User chose to **stop here** rather than add a shadow resolution knob / update throttle.

**Tooling added (kept):** env `CORFORGE_SCENE=<name>` picks startup scene (main.cpp); env `CORFORGE_PROFILE=1` dumps per-pass GPU/CPU ms to stderr every 60 frames (RendererManager render_frame). Profiler API: `stage_times()`, `stage_name()`, enum `PROF_*`.

**Shipped #2 (verified): per-frame UBO refactor.** Moved all pass-constant uniforms (view/proj/lights[8]/lightSpace2D[6]/viewPos/shadow/sky/fog/time/near/far/clip/screen/reflBoxMin) into a std140 `FrameBlock` UBO (binding 0), uploaded once per draw_scene instead of per-submesh. Killed the per-light `std::string` building in draw_submesh.
- C++: `FrameStd140`/`LightStd140` structs in RendererManager.cpp with `static_assert(sizeof==1264/80)` + offsetof guards; `init_frame_ubo`/`upload_frame_ubo`; UBO bound in init. draw_submesh now only sets per-object uniforms.
- GLSL: identical `FrameBlock` block + `Light` struct pasted into basic/water/terrain (.vert+.frag); uses `layout(std140, binding=0)` so no glUniformBlockBinding needed. uModel/uNormalMatrix/uCalm/material stay per-object.
- Result (CPU-bound scenes): TerrainDemo Scene CPU 1.65->0.49ms, FlockingDemo 0.81->0.40ms. Verified visually correct on TerrainDemo (terrain+shadows), MirrorRoom (PBR reflections), CoolPoolScene (water). std140 offsets confirmed correct (visuals would corrupt otherwise).
- NOTE: pre-existing "[Shader] uniform 'uMetallic/uCalm/...' not found" warnings persist (draw_submesh sets all material uniforms on every shader incl. terrain which lacks them) — harmless, predates this work.

**Untouched future levers (if revisited):** render-scale + MSAA knobs (Scene+Post ~6ms, all scenes); reflection-capture dirty-flag/budget/res (Captures ~2.5ms ShowcaseScene/MirrorRoom); shadow resolution knob / update throttle (moving-occluder scenes); per-object uniform-location caching (avoid string hash in Shader::location). See [[corforge-build-env]].
