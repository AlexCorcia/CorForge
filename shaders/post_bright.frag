#version 460 core

// Bloom bright-pass: keep only the HDR energy above a luminance threshold, with a
// soft knee so it ramps in gently instead of popping.
in vec2 vUV;
out vec4 frag;

uniform sampler2D uScene;
uniform float     uThreshold;

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float k = max(l - uThreshold, 0.0) / max(l, 1e-4); // fraction above threshold
    frag = vec4(c * k, 1.0);
}
