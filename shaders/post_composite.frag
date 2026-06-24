#version 460 core

// Final post step: fold AO and bloom into the linear-HDR scene, expose, tonemap
// (ACES), gamma-correct, and vignette. This is the ONLY place the scene is
// tonemapped now -- the scene shaders write linear HDR (uApplyGamma == 0).
in vec2 vUV;
out vec4 frag;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uAO;
uniform sampler2D uDepth;       // resolved scene depth (for distance fog)
uniform float     uBloomIntensity;
uniform float     uExposure;
uniform float     uVignette;
uniform int       uUseBloom;
uniform int       uUseAO;
uniform int       uUseFog;
uniform vec3      uFogColor;    // linear HDR
uniform float     uFogDensity;
uniform float     uNear;
uniform float     uFar;
uniform mat4      uInvProj; // for reconstructing the sky view ray (robust, near-plane)
uniform mat4      uInvView;

// Narkowicz 2015 ACES filmic approximation.
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float linearDepth(float d) {
    float z = d * 2.0 - 1.0;
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

void main() {
    vec3 hdr = texture(uScene, vUV).rgb;
    if (uUseAO == 1) hdr *= texture(uAO, vUV).r;

    // Fog: exponential distance fog on geometry, and a matching horizon haze on the
    // sky so the fogged ground blends seamlessly into the sky (no hard floor edge).
    if (uUseFog == 1) {
        float d = texture(uDepth, vUV).r;
        // Treat anything at/near the far plane as SKY. The skybox writes depth ~1.0
        // (gl_Position = p.xyww) but float imprecision at the cube edges drops some
        // pixels just under 1.0; a strict `< 1.0` test then sends those through the
        // geometry-fog branch -> a hard fog WEDGE along the skybox cube edges. The
        // epsilon keeps the whole sky on the sky-fog branch.
        if (d < 0.9999) {
            float dist = linearDepth(d);
            float f = 1.0 - exp(-uFogDensity * dist);
            hdr = mix(hdr, uFogColor, clamp(f, 0.0, 1.0));
        } else {
            // Sky: reconstruct the world view ray ROBUSTLY -- view-space ray via the
            // inverse projection at the near plane, then rotate to world. This avoids the
            // far-plane w-division degeneracy of the old uInvViewProj path, which made a
            // hard fog WEDGE in the sky (visible with strong/bright fog). Then haze the
            // lower hemisphere with a smooth, wide falloff toward the zenith.
            vec2 ndc = vUV * 2.0 - 1.0;
            vec3 viewRay = vec3(ndc.x * uInvProj[0][0], ndc.y * uInvProj[1][1], -1.0);
            vec3 dir = mat3(uInvView) * normalize(viewRay); // never flips (z stays -1)
            float f = exp(-max(dir.y, 0.0) * 1.3);
            hdr = mix(hdr, uFogColor, f);
        }
    }

    if (uUseBloom == 1) hdr += texture(uBloom, vUV).rgb * uBloomIntensity;

    hdr *= uExposure;
    vec3 c = aces(hdr);
    c = pow(c, vec3(1.0 / 2.2));

    // Smooth radial darkening toward the corners.
    float v = smoothstep(0.9, 0.3, length(vUV - 0.5));
    c *= mix(1.0, v, uVignette);

    frag = vec4(c, 1.0);
}
