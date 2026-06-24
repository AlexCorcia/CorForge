#version 460 core

// Heightmap terrain surface. The mesh (built by TerrainComponent) already has its
// heights baked in; this just transforms it and passes world pos/normal + the
// per-vertex normalised height (stored in uv.x) for height/slope colouring.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;     // uv.x = normalised height [0,1]
layout(location = 3) in vec3 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMatrix;
uniform vec4 uClipPlane;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vFragPos = world.xyz;
    vNormal  = normalize(uNormalMatrix * aNormal);
    vUV      = aUV;
    gl_ClipDistance[0] = dot(world, uClipPlane);
    gl_Position = uProj * uView * world;
}
