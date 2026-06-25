#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;

uniform mat4 uModel;
uniform mat3 uNormalMatrix;

// Pass-constant data shared by basic/water/terrain, bound once per pass as a
// std140 uniform buffer (binding 0). MUST stay byte-identical across all of them
// and match FrameStd140 in RendererManager.cpp. uClipPlane is world-space (n, d),
// only active when GL_CLIP_DISTANCE0 is enabled.
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

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vFragPos = world.xyz;
    vNormal  = uNormalMatrix * aNormal;
    vTangent = uNormalMatrix * aTangent;
    vUV      = aUV;
    gl_ClipDistance[0] = dot(world, uClipPlane);
    gl_Position = uProj * uView * world;
}
