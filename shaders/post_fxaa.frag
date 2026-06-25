#version 460 core

// FXAA (Fast Approximate Anti-Aliasing), ~3.11 style, run on the final LDR image.
// Cheap full-screen edge AA that complements or replaces MSAA: it finds luma edges
// and blends along them. Operates in gamma space; luma is the green-weighted approx.
in vec2 vUV;
out vec4 frag;

uniform sampler2D uImage;
uniform vec2      uTexel; // 1.0 / resolution

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec3 rgbM = texture(uImage, vUV).rgb;
    float lM  = luma(rgbM);
    float lNW = luma(texture(uImage, vUV + vec2(-1.0, -1.0) * uTexel).rgb);
    float lNE = luma(texture(uImage, vUV + vec2( 1.0, -1.0) * uTexel).rgb);
    float lSW = luma(texture(uImage, vUV + vec2(-1.0,  1.0) * uTexel).rgb);
    float lSE = luma(texture(uImage, vUV + vec2( 1.0,  1.0) * uTexel).rgb);

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    // Flat / low-contrast pixels have no visible edge -> leave them untouched.
    if (lMax - lMin < max(0.0312, lMax * 0.125)) { frag = vec4(rgbM, 1.0); return; }

    // Edge direction from the diagonal luma gradients.
    vec2 dir;
    dir.x = -((lNW + lNE) - (lSW + lSE));
    dir.y =  ((lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.0625, 1.0 / 128.0);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * uTexel;

    vec3 rgbA = 0.5 * (texture(uImage, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(uImage, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uImage, vUV + dir * -0.5).rgb +
                                     texture(uImage, vUV + dir *  0.5).rgb);
    float lB = luma(rgbB);
    // rgbB is the sharper 4-tap average; fall back to the 2-tap if it overshot.
    frag = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
