#version 460 core

// Water surface with vertical WAVES: sums a few directional sine waves to displace
// each vertex up/down and derives the surface normal analytically from their slope.
// Needs a subdivided plane (WaterComponent builds one). Pairs with water.frag.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;

uniform mat4  uModel;
uniform mat3  uNormalMatrix;
uniform float uCalm; // 0 = full ocean waves, 1 = nearly flat puddle (per-object)

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

out vec3 vNormal;
out vec3 vTangent;
out vec3 vFragPos;
out vec2 vUV;

void wave(vec2 dir, float amp, float len, float spd, vec2 p, float t,
          inout float h, inout vec2 slope) {
    dir = normalize(dir);
    float w = 6.2831853 / len;             // angular frequency
    float ph = dot(dir, p) * w + t * spd;
    h     += amp * sin(ph);
    slope += amp * w * cos(ph) * dir;      // d(h)/dp
}

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vec2 p = world.xz;

    float h = 0.0;
    vec2  slope = vec2(0.0);
    wave(vec2( 1.0,  0.4), 0.16, 5.0, 1.0, p, uTime, h, slope);
    wave(vec2(-0.6,  1.0), 0.11, 3.3, 1.3, p, uTime, h, slope);
    wave(vec2( 0.8, -0.7), 0.07, 2.1, 1.7, p, uTime, h, slope);
    wave(vec2( 0.2,  1.0), 0.05, 1.4, 2.2, p, uTime, h, slope);
    // Calm puddles keep only a faint ripple (12% of the ocean displacement).
    float amp = mix(1.0, 0.12, clamp(uCalm, 0.0, 1.0));
    world.y += h * amp;
    slope   *= amp;

    vFragPos = world.xyz;
    vNormal  = normalize(vec3(-slope.x, 1.0, -slope.y)); // world-space wave normal
    vTangent = uNormalMatrix * aTangent;
    vUV      = aUV;
    gl_ClipDistance[0] = dot(world, uClipPlane);
    gl_Position = uProj * uView * world;
}
