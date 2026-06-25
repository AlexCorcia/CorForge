#version 460 core

// Depth-of-field: blur the linear-HDR scene by a circle-of-confusion (CoC) derived
// from how far each pixel's depth is from the focus plane. A golden-angle spiral
// disk gather gives a smooth bokeh; the blur radius scales with the centre pixel's
// CoC so in-focus pixels stay sharp. Runs before tonemap so highlights bloom nicely.
in vec2 vUV;
out vec4 frag;

uniform sampler2D uScene;   // linear HDR
uniform sampler2D uDepth;   // resolved scene depth
uniform vec2      uTexel;   // 1 / resolution
uniform float     uNear;
uniform float     uFar;
uniform float     uFocusDist;   // view-space distance kept sharp
uniform float     uFocusRange;  // distance over which it ramps to full blur
uniform float     uMaxRadius;   // max bokeh radius in pixels

float linearDepth(float d) {
    float z = d * 2.0 - 1.0;
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

float coc(float depthSample) {
    float dist = linearDepth(depthSample);
    return clamp(abs(dist - uFocusDist) / max(uFocusRange, 1e-3), 0.0, 1.0);
}

void main() {
    float centerCoC = coc(texture(uDepth, vUV).r);
    float radius = centerCoC * uMaxRadius;

    vec3 center = texture(uScene, vUV).rgb;
    if (radius < 0.5) { frag = vec4(center, 1.0); return; }

    vec3 sum = center;
    float wsum = 1.0;
    const int N = 24;
    for (int i = 0; i < N; ++i) {
        float a = float(i) * 2.39996323;                 // golden angle
        float r = sqrt((float(i) + 0.5) / float(N)) * radius;
        vec2 off = vec2(cos(a), sin(a)) * r * uTexel;
        // Weight by the sample's own CoC so sharp (in-focus) foreground doesn't
        // bleed into the blurred background as a halo.
        float w = coc(texture(uDepth, vUV + off).r);
        sum += texture(uScene, vUV + off).rgb * w;
        wsum += w;
    }
    frag = vec4(sum / wsum, 1.0);
}
