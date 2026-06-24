#version 460 core

// Stylised toon water (flowing cel bands + pool-edge / shore foam), now also LIT:
// it receives the scene lights and shadows so it darkens under objects and picks up
// the light colour, while keeping the flat cartoon bands. Pairs with water.vert.

in vec3 vNormal;
in vec3 vTangent;
in vec3 vFragPos;
in vec2 vUV;

out vec4 FragColor;

uniform vec3  uViewPos;
uniform vec3  uAlbedo;      // base water colour
uniform float uOpacity;
uniform vec2  uScreenSize;
uniform float uTime;
uniform float uCalm; // 0 = ocean, 1 = flat calm puddle
uniform int   uApplyGamma;
uniform int   uApplyFog;    // in-shader fog for reflection captures
uniform vec3  uFogColor;
uniform float uFogDensity;

uniform sampler2D uDepthTex; // resolved opaque scene depth (0/1 = none)
uniform float uNear;
uniform float uFar;

// --- lights + shadows (same bindings drawSubmesh sets for every shader) -------
#define MAX_LIGHTS 8
#define MAX_SHADOW_2D 6
struct Light {
    int   type; vec3 color; float intensity; vec3 position; vec3 direction;
    float range; float cosInner; float cosOuter; int shadow2DIndex; int shadowCubeIndex;
};
uniform int   uNumLights;
uniform Light uLights[MAX_LIGHTS];
uniform sampler2DArray   uShadow2D;
uniform samplerCubeArray uShadowCube;
uniform sampler2DArray   uShadow2DColor;
uniform samplerCubeArray uShadowCubeColor;
uniform mat4  uLightSpace2D[MAX_SHADOW_2D];
uniform float uShadowStrength;

float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.55;
    for (int i = 0; i < 5; ++i) { v += a * vnoise(p); p = p * 2.0 + 19.1; a *= 0.5; }
    return v;
}
float linearize(float d) {
    float z = d * 2.0 - 1.0;
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

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

    float calm = clamp(uCalm, 0.0, 1.0);

    // --- flowing pattern: domain-warped fbm, animated -> organic moving cells -
    vec2 p = vFragPos.xz * 0.85;
    float t = uTime * mix(0.18, 0.05, calm); // calmer puddles drift slowly
    vec2 q = vec2(fbm(p + vec2(0.0, 2.0 * t) + 1.3), fbm(p + vec2(5.1, -2.0 * t) + 7.7));
    float n = fbm(p + 2.4 * q + vec2(t, -0.7 * t));

    // 3-tone CEL posterize with HARD edges (illustrated look).
    vec3 deep   = uAlbedo;
    vec3 mid    = mix(uAlbedo, vec3(0.30, 0.70, 1.0), 0.55);
    vec3 bright = mix(uAlbedo, vec3(0.50, 0.82, 1.0), 0.70);
    float e1 = 0.45, e2 = 0.64;
    vec3 color = mix(deep, mid, smoothstep(e1 - 0.012, e1 + 0.012, n));
    color = mix(color, bright, smoothstep(e2 - 0.012, e2 + 0.012, n));
    float outline = max(1.0 - smoothstep(0.0, 0.02, abs(n - e1)),
                        1.0 - smoothstep(0.0, 0.02, abs(n - e2)));
    color = mix(color, mid, outline * 0.5);
    // Calm puddles: fade the bright flowing bands toward a uniform deep tint.
    color = mix(color, deep, calm * 0.7);

    // --- white foam: thin pool edge + crisp shore band at submerged geometry ---
    vec2 suv = gl_FragCoord.xy / uScreenSize;
    float sd = texture(uDepthTex, suv).r;
    float sceneZ = (sd > 0.0 && sd < 1.0) ? linearize(sd) : 1e6;
    float waterDepth = max(sceneZ - linearize(gl_FragCoord.z), 0.0);

    float edgeDist = min(min(vUV.x, 1.0 - vUV.x), min(vUV.y, 1.0 - vUV.y));
    float wobE = vnoise(vFragPos.xz * 5.0 + uTime * 0.5) * 0.012;
    float border = 1.0 - smoothstep(0.012 + wobE, 0.030 + wobE, edgeDist);
    float wobS = (vnoise(vFragPos.xz * 7.0 - uTime * 0.5) - 0.5) * 0.08;
    float shore = 1.0 - smoothstep(0.20 + wobS, 0.26 + wobS, waterDepth);
    shore *= (1.0 - calm); // thin puddles sit right on the floor -> no shore foam
    float foam = clamp(max(border, shore), 0.0, 1.0);
    color = mix(color, vec3(1.0), foam);

    // --- toon lighting: scene lights + shadows modulate the flat colour --------
    vec3 light = vec3(0.45); // ambient floor (stays readable in shadow)
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
        vec3 lit = mix(vec3(1.0), trans, uShadowStrength); // shadow tint
        light += lt.color * lt.intensity * atten * ndl * lit * 0.55;
    }
    color *= clamp(light, 0.0, 1.6);

    float td = clamp(waterDepth / 3.0, 0.0, 1.0);
    float alpha = max(mix(uOpacity, 1.0, clamp(td * 3.0, 0.0, 1.0)), foam);

    // Distance fog (reflection captures only; the main view fogs in post).
    if (uApplyFog == 1) {
        float fogF = 1.0 - exp(-uFogDensity * length(vFragPos - uViewPos));
        color = mix(color, uFogColor, clamp(fogF, 0.0, 1.0));
    }

    if (uApplyGamma == 1)
        color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(color, alpha);
}
