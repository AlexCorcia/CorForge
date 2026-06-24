#version 460 core
// Selection-outline hull. Each vertex is pushed radially away from the object's
// world centre by `uThickness * viewDistance`, so the expanded shell sits a
// constant width OUTSIDE the silhouette on screen (independent of depth) and stays
// connected on hard-edged meshes (cubes) where per-face normal extrusion would gap.
layout(location = 0) in vec3 aPos;

uniform mat4  uModel;
uniform mat4  uView;
uniform mat4  uProj;
uniform vec3  uCenter;    // world-space object centre
uniform float uThickness; // 0 for the silhouette pass, >0 for the expanded hull

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vec3 dir = world.xyz - uCenter;
    float len = length(dir);
    dir = (len > 1e-5) ? dir / len : vec3(0.0);
    float viewDist = max(-(uView * world).z, 0.1); // distance in front of the camera
    world.xyz += dir * (uThickness * viewDist);
    gl_Position = uProj * uView * world;
}
