#version 460 core

// Separable 9-tap Gaussian blur. Run once horizontally then once vertically
// (uDir = texelSize * axis). Used for both the bloom and the SSAO blur.
in vec2 vUV;
out vec4 frag;

uniform sampler2D uTex;
uniform vec2      uDir; // (1/width, 0) or (0, 1/height), scaled by step

void main() {
    const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 c = texture(uTex, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        c += texture(uTex, vUV + uDir * float(i)).rgb * w[i];
        c += texture(uTex, vUV - uDir * float(i)).rgb * w[i];
    }
    frag = vec4(c, 1.0);
}
