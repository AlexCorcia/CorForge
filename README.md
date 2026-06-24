# CorForge

A 3D showcase engine written from scratch in C++20 with OpenGL 4.6 — an
Object/Component scene model, singleton managers, a Dear ImGui docking editor,
its own physics, and runtime-loaded GLSL shaders. No game-engine dependencies:
just GLFW, GLAD, glm, Dear ImGui, Assimp/tinygltf, stb, and nlohmann/json.

## Build

Requires Visual Studio 2022/2026 (or Build Tools) with the **Desktop development
with C++** workload. Dependencies are fetched automatically by CMake on the first
configure, so the first build needs an internet connection.

```powershell
# Configure, build (Release), and run:
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Run

# Other options:
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Debug
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Clean   # wipe build dir first
```

The script imports the MSVC environment and uses the CMake + Ninja bundled with
Visual Studio, so nothing extra needs to be on your PATH. Shaders are loaded at
runtime from `shaders/`, so editing a shader only needs an app restart, not a
rebuild.

**Controls:** hold **right mouse** to look around, **W/A/S/D** to move, **Q/E**
down/up, **Left Shift** to sprint, **Esc** to quit. Click an object to select it;
**1/2/3** switch the move/rotate/scale gizmo; **Delete** removes the selection.

## Features

**Rendering**
- Cook–Torrance metallic-roughness PBR (GGX + Smith + Fresnel) per light, flat
  ambient term, Reinhard/ACES tonemapping.
- Shadows for every light type: directional/spot via a 2D depth-texture array,
  point lights via a cube-map array; PCF + slope-scaled bias. Translucent,
  coloured shadows from transparent occluders (transmittance maps).
- Reflections: per-object scene cubemap capture, plus exact planar and box
  (6-face) mirrors; roughness-weighted blur.
- Sky / IBL: procedural or equirectangular environment cubemap, skybox, cheap
  image-based lighting; normal mapping with auto-computed tangents.
- HDR post-processing: MSAA RGBA16F target → depth-only SSAO → bloom → single
  ACES composite with exposure, vignette and distance/height fog.

**Geometry & content**
- Primitives (cube, sphere, plane, pyramid, disk, octahedron) plus model loading:
  `.gltf`/`.glb` (tinygltf) and `.obj`/`.fbx`/etc. (Assimp) as one object with a
  submesh per material. Drop a model folder in `assets/models/` (scanned
  recursively); spawn via **Add → Model**.
- Procedural terrain (`TerrainComponent`): fbm heightmap with low-poly or smooth
  styles, a slab/diorama cliff base, donut/atoll mode, height+slope colouring
  (sand/grass/rock/snow), scattered low-poly trees & rocks (forest clustering),
  and a heightfield collider so bodies rest on and roll over the surface.
- Water (`WaterComponent`): animated waves, calm/puddle control, shore foam, and
  automatic splashes when rigidbodies break the surface.

**Simulation**
- From-scratch physics: semi-implicit Euler, impulse solver with restitution +
  friction, angular velocity + inertia tensors, oriented box-box (SAT + face
  clipping). Colliders: sphere / box / plane / terrain heightfield.
- Buoyancy, cloth (PBD/Verlet), CPU particles (instanced billboards: fire, smoke,
  rain, snow, portals, lightning, …), and boids flocking that composes with the
  physics/buoyancy components.

**Editor & workflow**
- Dear ImGui docking layout: Hierarchy (with drag-drop parenting), Inspector,
  Stats, Scenes, Prefabs; ImGuizmo transform gizmos.
- Object hierarchy / parenting (world matrices walk the chain).
- Scenes: JSON save/load of every object + component to `assets/scenes/`.
- Prefabs: save any object subtree as a reusable template under
  `assets/prefabs/` and spawn copies from the Prefabs panel.

## Layout

```
src/             engine + app source (core / components / gfx / ui)
shaders/         GLSL programs (loaded at runtime)
assets/models/   .gltf/.glb/.obj/.fbx model folders (scanned recursively)
assets/meshes/   small .obj primitives selectable in the editor
assets/textures/ image files for albedo / normal maps
assets/scenes/   saved scenes (JSON)
assets/prefabs/  saved prefabs (JSON)
scripts/         build + model-convert helpers
tools/           one-off generators / utilities
build/           (generated) CMake output; binary in build/bin
```

Demo scenes live in `assets/scenes/` (e.g. `DonutDemo`, `TerrainDemo`,
`FlockingDemo`, `ParticleDemo`, `RainDemo`, `MirrorRoom`) — load them from the
**Scenes** panel. The startup scene is set in `src/main.cpp`.
