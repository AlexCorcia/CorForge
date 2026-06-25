#version 460 core

// Heightmap terrain surface. The mesh (built by TerrainComponent) already has its
// heights baked in; this just transforms it and passes world pos/normal + the
// per-vertex normalised height (stored in uv.x) for height/slope colouring.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;     // uv.x = normalised height [0,1]
layout(location = 3) in vec3 aTangent;

uniform mat4 uModel;
uniform mat3 uNormalMatrix;

// Shared pass-constant data (see basic.vert). Must stay byte-identical across
// basic/water/terrain and match FrameStd140 in RendererManager.cpp.
#define MAX_LIGHTS 8
#define MAX_SHADOW_2D 6
struct Light {
    int type; vec3 color; float intensity; vec3 position; vec3 direction;
    float range; float cosInner; float cosOuter; int shadow2DIndex; int shadowCubeIndex;
};
layout(std140, binding = 0) uniform FrameBlock {
    mat4  uView;
    mat4  uProj;
    mat4  uLightSpace2D[MAX_SHADOW_2D];
    vec4  uClipPlane;
    vec3  uViewPos;     float uShadowStrength;
    vec3  uReflBoxMin;  float uEnvMaxMip;
    vec3  uFogColor;    float uFogDensity;
    vec2  uScreenSize;  float uSkyIntensity; float uTime;
    float uNear; float uFar; int uNumLights; int uHasSky;
    int   uApplyGamma; int uApplyFog; int uPad0; int uPad1;
    Light uLights[MAX_LIGHTS];
};

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
