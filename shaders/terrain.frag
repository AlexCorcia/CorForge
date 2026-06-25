#version 460 core

// Procedural terrain shading: colour by NORMALISED HEIGHT (vUV.x, baked per vertex)
// and SLOPE (from the normal) into sand/grass/rock/snow bands, with rock on steep
// faces, then lit by the scene lights + shadows (same bindings drawSubmesh sets).
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vUV;

out vec4 FragColor;

// Shared pass-constant data (see basic.frag). Must stay byte-identical across
// basic/water/terrain and match FrameStd140 in RendererManager.cpp.
#define MAX_LIGHTS 8
#define MAX_SHADOW_2D 6
struct Light {
    int   type; vec3 color; float intensity; vec3 position; vec3 direction;
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

// --- per-object material uniforms -------------------------------------------
uniform vec3  uAlbedo;   // grass tint (material albedo)
uniform float uAmbient;

uniform sampler2DArray   uShadow2D;
uniform samplerCubeArray uShadowCube;
uniform sampler2DArray   uShadow2DColor;
uniform samplerCubeArray uShadowCubeColor;

float hash(vec2 p){ p=fract(p*vec2(123.34,456.21)); p+=dot(p,p+45.32); return fract(p.x*p.y); }
float vnoise(vec2 p){ vec2 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y); }

vec3 shadow2D(int idx, vec3 N, vec3 L) {
    vec4 lp = uLightSpace2D[idx] * vec4(vFragPos, 1.0);
    vec3 proj = lp.xyz / lp.w * 0.5 + 0.5;
    if (proj.z > 1.0) return vec3(1.0);
    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0008);
    vec2 texel = 1.0 / vec2(textureSize(uShadow2D, 0).xy);
    vec3 t = vec3(0.0);
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y) {
            vec3 uvw = vec3(proj.xy + vec2(x, y) * texel, float(idx));
            float closest = texture(uShadow2D, uvw).r;
            t += (proj.z - bias > closest) ? texture(uShadow2DColor, uvw).rgb : vec3(1.0);
        }
    return t / 25.0;
}
vec3 shadowCube(Light lt) {
    vec3 frto = vFragPos - lt.position;
    float current = length(frto) / lt.range;
    const vec3 offs[20] = vec3[](
        vec3( 1, 1, 1), vec3( 1,-1, 1), vec3(-1,-1, 1), vec3(-1, 1, 1),
        vec3( 1, 1,-1), vec3( 1,-1,-1), vec3(-1,-1,-1), vec3(-1, 1,-1),
        vec3( 1, 1, 0), vec3( 1,-1, 0), vec3(-1,-1, 0), vec3(-1, 1, 0),
        vec3( 1, 0, 1), vec3(-1, 0, 1), vec3( 1, 0,-1), vec3(-1, 0,-1),
        vec3( 0, 1, 1), vec3( 0,-1, 1), vec3( 0,-1,-1), vec3( 0, 1,-1));
    vec3 t = vec3(0.0);
    for (int i = 0; i < 20; ++i) {
        vec4 dir = vec4(frto + offs[i] * 0.015 * lt.range, float(lt.shadowCubeIndex));
        float closest = texture(uShadowCube, dir).r;
        t += (current - 0.015 > closest) ? texture(uShadowCubeColor, dir).rgb : vec3(1.0);
    }
    return t / 20.0;
}

void main() {
    vec3 N = normalize(vNormal);
    float h = clamp(vUV.x, 0.0, 1.0);     // normalised height
    float slope = 1.0 - clamp(N.y, 0.0, 1.0); // 0 = flat, 1 = vertical

    // A little noise breaks up the flat colour bands.
    float n = vnoise(vFragPos.xz * 0.6) * 0.10 - 0.05;

    vec3 sand  = vec3(0.78, 0.71, 0.50);
    vec3 grass = uAlbedo;                  // material albedo tints the grass
    vec3 rock  = vec3(0.42, 0.40, 0.38);
    vec3 snow  = vec3(0.92, 0.94, 0.97);

    // Height bands: sand near the bottom -> grass -> rock -> snow at the peaks.
    vec3 col = sand;
    col = mix(col, grass, smoothstep(0.04, 0.16, h + n));
    col = mix(col, rock,  smoothstep(0.62, 0.80, h + n));
    col = mix(col, snow,  smoothstep(0.88, 0.96, h + n));
    // Steep faces are bare rock regardless of height.
    col = mix(col, rock,  smoothstep(0.55, 0.78, slope));

    // Slab sides (uv.y marker): stratified dirt/clay cliff, like a diorama base.
    float cliff = smoothstep(0.5, 0.9, vUV.y);
    if (cliff > 0.0) {
        float layer = sin(vFragPos.y * 3.5) * 0.5 + 0.5;          // horizontal sediment bands
        vec3 dirt = mix(vec3(0.45, 0.32, 0.21), vec3(0.32, 0.22, 0.14), layer);
        dirt += (vnoise(vFragPos.xz * 1.3 + vFragPos.y) * 0.10 - 0.05);
        col = mix(col, dirt, cliff);
    }

    // Lighting: scene lights + coloured shadows (Lambert, soft toon-ish ambient).
    vec3 light = vec3(uAmbient + 0.18);
    for (int i = 0; i < uNumLights && i < MAX_LIGHTS; ++i) {
        Light lt = uLights[i];
        vec3 L; float atten = 1.0;
        if (lt.type == 0) {
            L = normalize(-lt.direction);
        } else {
            vec3 toL = lt.position - vFragPos;
            float dist = length(toL);
            L = toL / max(dist, 1e-4);
            float a = clamp(1.0 - dist / lt.range, 0.0, 1.0);
            atten = a * a;
            if (lt.type == 2) {
                float th = dot(normalize(lt.direction), -L);
                atten *= clamp((th - lt.cosOuter) / max(lt.cosInner - lt.cosOuter, 1e-4), 0.0, 1.0);
            }
        }
        float ndl = max(dot(N, L), 0.0);
        vec3 trans = vec3(1.0);
        if (lt.shadow2DIndex >= 0)        trans = shadow2D(lt.shadow2DIndex, N, L);
        else if (lt.shadowCubeIndex >= 0) trans = shadowCube(lt);
        vec3 lit = mix(vec3(1.0), trans, uShadowStrength);
        light += lt.color * lt.intensity * atten * ndl * lit;
    }
    col *= clamp(light, 0.0, 2.0);

    // Distance fog (reflection captures only; the main view fogs in post).
    if (uApplyFog == 1) {
        float fogF = 1.0 - exp(-uFogDensity * length(vFragPos - uViewPos));
        col = mix(col, uFogColor, clamp(fogF, 0.0, 1.0));
    }

    if (uApplyGamma == 1) {
        col = col / (col + vec3(1.0));        // Reinhard tonemap (matches basic.frag)
        col = pow(col, vec3(1.0 / 2.2));
    }
    FragColor = vec4(col, 1.0);
}
