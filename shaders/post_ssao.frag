#version 460 core

// Screen-space ambient occlusion from the depth buffer alone (no G-buffer).
// View-space position is reconstructed from depth via the inverse projection, and
// the surface normal from its screen-space derivatives. A randomly-rotated
// hemisphere kernel is sampled and compared against the depth buffer; the more
// neighbouring samples sit in front of real geometry, the more occluded the pixel.
in vec2 vUV;
out vec4 frag;

uniform sampler2D uDepth;   // resolved scene depth (DEPTH24_STENCIL8, .r in [0,1])
uniform mat4      uProj;
uniform mat4      uInvProj;
uniform vec2      uScreen;
uniform float     uRadius;
uniform float     uBias;
uniform float     uStrength;

vec3 viewPos(vec2 uv) {
    float d = texture(uDepth, uv).r;
    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = uInvProj * clip;
    return v.xyz / v.w;
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
    float d = texture(uDepth, vUV).r;
    if (d >= 1.0) { frag = vec4(1.0); return; } // sky / background -> unoccluded

    vec3 P = viewPos(vUV);
    vec3 N = normalize(cross(dFdx(P), dFdy(P))); // faceted, but fine for AO

    // Per-pixel random rotation of the kernel to trade banding for noise (blurred out).
    float rnd = hash(vUV * uScreen) * 6.2831853;
    vec3 rvec = vec3(cos(rnd), sin(rnd), 0.0);
    vec3 T = normalize(rvec - N * dot(rvec, N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    const int SAMPLES = 16;
    float occ = 0.0;
    for (int i = 0; i < SAMPLES; ++i) {
        float a = hash(vUV * uScreen + float(i) * 1.37);
        float b = hash(vUV * uScreen + float(i) * 2.71 + 11.0);
        float c = hash(vUV * uScreen + float(i) * 3.14 + 23.0);
        vec3 s = normalize(vec3(a * 2.0 - 1.0, b * 2.0 - 1.0, c)); // hemisphere (+z)
        float scale = float(i) / float(SAMPLES);
        s *= mix(0.1, 1.0, scale * scale);                         // cluster near origin

        vec3 samplePos = P + TBN * s * uRadius;
        vec4 off = uProj * vec4(samplePos, 1.0);
        off.xyz /= off.w;
        vec2 suv = off.xy * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;

        float sceneZ = viewPos(suv).z; // view-space z (negative, -z = forward)
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / max(abs(P.z - sceneZ), 1e-4));
        if (sceneZ >= samplePos.z + uBias) occ += rangeCheck;
    }

    float ao = 1.0 - (occ / float(SAMPLES)) * uStrength;
    frag = vec4(vec3(clamp(ao, 0.0, 1.0)), 1.0);
}
