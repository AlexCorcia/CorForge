#version 460 core

in vec3 vNormal;
in vec3 vTangent;
in vec3 vFragPos;
in vec2 vUV;

// Shared pass-constant data (view/proj/lights/shadows/sky/fog), bound once per
// pass as a std140 UBO (binding 0). MUST stay byte-identical across basic/water/
// terrain and match FrameStd140 in RendererManager.cpp.
#define MAX_LIGHTS 8
#define MAX_SHADOW_2D 6
struct Light {
    int   type;        // 0 directional, 1 point, 2 spot
    vec3  color;
    float intensity;
    vec3  position;
    vec3  direction;
    float range;
    float cosInner;
    float cosOuter;
    int   shadow2DIndex;
    int   shadowCubeIndex;
};
layout(std140, binding = 0) uniform FrameBlock {
    mat4  uView;
    mat4  uProj;
    mat4  uLightSpace2D[MAX_SHADOW_2D];
    vec4  uClipPlane;
    vec3  uViewPos;     float uShadowStrength;
    vec3  uReflBoxMin;  float uEnvMaxMip;    // scene AABB min (.y = ground level)
    vec3  uFogColor;    float uFogDensity;
    vec2  uScreenSize;  float uSkyIntensity; float uTime;
    float uNear; float uFar; int uNumLights; int uHasSky;
    int   uApplyGamma; int uApplyFog; int uPad0; int uPad1;
    Light uLights[MAX_LIGHTS];
};

// --- per-object material uniforms -------------------------------------------
uniform vec3      uAlbedo;
uniform float     uAmbient;
uniform sampler2D uAlbedoMap;       // sRGB
uniform vec2      uUvScale;

uniform float     uMetallic;
uniform float     uRoughness;
uniform sampler2D uMetalRoughMap;   // linear: G=roughness, B=metallic
uniform int       uHasMRMap;

uniform sampler2D uNormalMap;       // tangent-space normals (linear)
uniform int       uHasNormalMap;

// Sky / image-based lighting environment (linear HDR cubemap, mipmapped).
uniform samplerCube uEnvCube;
uniform float       uEnvSpecular; // scales the IBL mirror term (0 = matte)

// Per-object environment reflection (captured cubemap).
uniform int         uReflective;
uniform float       uReflectivity;
uniform samplerCube uReflCube;
uniform vec3        uReflProbePos;  // where the cubemap was captured
uniform float       uOpacity;

// Planar reflection (flat mirror surfaces). uPlanarMode: 0 = cubemap, 1 = single
// plane (samples uPlanarTex by screen position), 2 = box (6 faces in an array;
// the fragment picks the face whose normal it matches). Exact, no distortion.
uniform int         uPlanarMode;
uniform sampler2D   uPlanarTex;
uniform sampler2DArray uPlanarArray;
uniform vec3        uFaceN[6];   // box face world normals (mode 2)

uniform sampler2DArray   uShadow2D;
uniform samplerCubeArray uShadowCube;
uniform sampler2DArray   uShadow2DColor;   // transmittance (translucent/coloured shadows)
uniform samplerCubeArray uShadowCubeColor;

out vec4 FragColor;

const float PI = 3.14159265359;

float distributionGGX(vec3 N, vec3 H, float rough) {
    float a = rough * rough;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float geometrySchlickGGX(float NdotV, float rough) {
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float geometrySmith(vec3 N, vec3 V, vec3 L, float rough) {
    return geometrySchlickGGX(max(dot(N, V), 0.0), rough) *
           geometrySchlickGGX(max(dot(N, L), 0.0), rough);
}
vec3 fresnelSchlick(float cosT, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0);
}
vec3 fresnelSchlickRough(float cosT, vec3 F0, float rough) {
    return F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0);
}

// Parallax-correct the reflection against the GROUND PLANE (the dominant
// reflector in an open scene). A box projection would assume a closed room and
// fold upward rays against an imaginary ceiling; instead, rays heading down are
// re-anchored to where they actually hit the floor, while rays heading up/outward
// sample the cubemap directly (their target -- background/distant geometry -- is
// effectively far away, so no parallax is needed).
vec3 parallaxCorrect(vec3 R, vec3 fragPos) {
    float floorY = uReflBoxMin.y;
    if (R.y < -1e-3) {
        float t = (floorY - fragPos.y) / R.y;
        if (t > 0.0) return (fragPos + R * t) - uReflProbePos;
    }
    return R;
}

// Returns the transmittance: how much (and what colour) light reaches the
// fragment through occluders. vec3(1) = fully lit, vec3(0) = full opaque shadow,
// tinted = behind coloured glass.
vec3 shadow2D(int idx, vec3 N, vec3 L) {
    vec4 lp = uLightSpace2D[idx] * vec4(vFragPos, 1.0);
    vec3 proj = lp.xyz / lp.w * 0.5 + 0.5;
    if (proj.z > 1.0) return vec3(1.0);
    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0008);
    vec2 texel = 1.0 / vec2(textureSize(uShadow2D, 0).xy);
    // 5x5 PCF: each occluded tap contributes its occluder's transmittance.
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
    // 20-tap PCF around the sample direction for a soft point-light penumbra.
    const vec3 offs[20] = vec3[](
        vec3( 1, 1, 1), vec3( 1,-1, 1), vec3(-1,-1, 1), vec3(-1, 1, 1),
        vec3( 1, 1,-1), vec3( 1,-1,-1), vec3(-1,-1,-1), vec3(-1, 1,-1),
        vec3( 1, 1, 0), vec3( 1,-1, 0), vec3(-1,-1, 0), vec3(-1, 1, 0),
        vec3( 1, 0, 1), vec3(-1, 0, 1), vec3( 1, 0,-1), vec3(-1, 0,-1),
        vec3( 0, 1, 1), vec3( 0,-1, 1), vec3( 0,-1,-1), vec3( 0, 1,-1));
    float radius = 0.015;
    vec3 t = vec3(0.0);
    for (int i = 0; i < 20; ++i) {
        vec4 dir = vec4(frto + offs[i] * radius * lt.range, float(lt.shadowCubeIndex));
        float closest = texture(uShadowCube, dir).r;
        t += (current - 0.015 > closest) ? texture(uShadowCubeColor, dir).rgb : vec3(1.0);
    }
    return t / 20.0;
}

void main() {
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;

    // Normal mapping: perturb N by the tangent-space normal map via the TBN basis.
    if (uHasNormalMap == 1) {
        vec3 T = normalize(vTangent - N * dot(N, vTangent)); // re-orthogonalize
        vec3 B = cross(N, T);
        vec3 nm = texture(uNormalMap, vUV * uUvScale).rgb * 2.0 - 1.0;
        N = normalize(mat3(T, B, N) * nm);
    }

    vec3 V = normalize(uViewPos - vFragPos);

    vec3 albedo = texture(uAlbedoMap, vUV * uUvScale).rgb * uAlbedo;
    float metallic = uMetallic;
    float roughness = uRoughness;
    if (uHasMRMap == 1) {
        vec3 mr = texture(uMetalRoughMap, vUV * uUvScale).rgb;
        roughness *= mr.g;
        metallic *= mr.b;
    }
    roughness = clamp(roughness, 0.045, 1.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < uNumLights && i < MAX_LIGHTS; ++i) {
        Light lt = uLights[i];
        vec3 L;
        float atten = 1.0;
        if (lt.type == 0) {
            L = normalize(-lt.direction);
        } else {
            vec3 toL = lt.position - vFragPos;
            float dist = length(toL);
            L = toL / max(dist, 1e-4);
            float a = clamp(1.0 - dist / lt.range, 0.0, 1.0);
            atten = a * a;
            if (lt.type == 2) {
                float theta = dot(normalize(lt.direction), -L);
                atten *= clamp((theta - lt.cosOuter) / max(lt.cosInner - lt.cosOuter, 1e-4), 0.0, 1.0);
            }
        }
        vec3 radiance = lt.color * lt.intensity * atten;

        vec3 H = normalize(V + L);
        float NDF = distributionGGX(N, H, roughness);
        float G   = geometrySmith(N, V, L, roughness);
        vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

        float NdotL = max(dot(N, L), 0.0);
        vec3 spec = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001);
        vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);

        // Transmittance through occluders (vec3(1) lit, 0 shadow, tinted glass).
        vec3 trans = vec3(1.0);
        if (lt.shadow2DIndex >= 0)        trans = shadow2D(lt.shadow2DIndex, N, L);
        else if (lt.shadowCubeIndex >= 0) trans = shadowCube(lt);
        vec3 lit = mix(vec3(1.0), trans, uShadowStrength); // coloured, translucent shadow

        // (diffuse not divided by PI so existing light intensities stay similar)
        Lo += (kd * albedo + spec) * radiance * NdotL * lit;
    }

    // --- Ambient + reflection --------------------------------------------
    // Image-based lighting from the sky cubemap: a blurry mip approximates the
    // diffuse irradiance, and a roughness-selected mip the specular reflection.
    vec3 ambient;
    if (uHasSky == 1) {
        float NdotV = max(dot(N, V), 0.0);
        vec3 kS = fresnelSchlickRough(NdotV, F0, roughness);
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        // Cap the LOD so we never sample the tiny top mips (1x1/2x2 = only 6
        // colours per cube), which otherwise show as blocky facets on rough/metal
        // surfaces. Keep at least an 8x8 face (maxMip-3) for diffuse + rough spec.
        float diffuseLod = max(uEnvMaxMip - 3.0, 0.0);
        float specLod    = min(roughness * uEnvMaxMip, max(uEnvMaxMip - 3.0, 0.0));
        vec3 irradiance = textureLod(uEnvCube, N, diffuseLod).rgb;
        vec3 R = reflect(-V, N);
        vec3 envSpec = textureLod(uEnvCube, R, specLod).rgb;
        // uEnvSpecular scales the mirror (specular) term; diffuse sky lighting stays.
        ambient = (kD * irradiance * albedo + kS * envSpec * uEnvSpecular) * uSkyIntensity;
    } else {
        ambient = albedo * uAmbient;
    }
    vec3 color = ambient + Lo;   // normal lit surface

    // Reflective objects blend the surface toward a mirror reflection by
    // reflectivity, so reflectivity = 1 is a true mirror (no diffuse showing
    // through) instead of the lit albedo sitting on top of the reflection.
    if (uReflective == 1 && uReflectivity > 0.0) {
        vec3 refl;
        vec2 suv = gl_FragCoord.xy / uScreenSize;
        if (uPlanarMode == 1) {
            refl = texture(uPlanarTex, suv).rgb;               // flat mirror (exact)
        } else if (uPlanarMode == 2) {
            // Pick the box face whose world normal best matches this fragment.
            vec3 nn = normalize(N);
            int best = 0; float bd = -2.0;
            for (int i = 0; i < 6; ++i) {
                float dd = dot(nn, uFaceN[i]);
                if (dd > bd) { bd = dd; best = i; }
            }
            refl = texture(uPlanarArray, vec3(suv, float(best))).rgb;
        } else {
            vec3 R = reflect(-V, N);
            vec3 Rc = parallaxCorrect(R, vFragPos);
            refl = textureLod(uReflCube, Rc, roughness * 5.0).rgb;
        }
        color = mix(color, refl, uReflectivity);
    }

    // (Selection is shown by a stencil outline pass, not a shaded rim glow.)

    // Distance fog (reflection captures only; the main view fogs in post).
    if (uApplyFog == 1) {
        float fogF = 1.0 - exp(-uFogDensity * length(vFragPos - uViewPos));
        color = mix(color, uFogColor, clamp(fogF, 0.0, 1.0));
    }

    // Tonemap + gamma only for the final screen pass. Reflection captures
    // (uApplyGamma == 0) store linear HDR so the mirror mixes real radiance and
    // the scene is tonemapped exactly once -- otherwise lit areas (light pools,
    // highlights) come out dim/flat in reflections from double tonemapping.
    if (uApplyGamma == 1) {
        color = color / (color + vec3(1.0));     // Reinhard tonemap
        color = pow(color, vec3(1.0 / 2.2));     // gamma
    }
    float alpha = uOpacity * texture(uAlbedoMap, vUV * uUvScale).a;
    FragColor = vec4(color, alpha);
}
