#version 460 core

in vec3 vWorld;

out vec4 FragColor;

uniform vec3  uLightPos;
uniform float uFar;
uniform vec3  uOccColor; // occluder albedo (the tint)
uniform float uOpacity;  // 1 = opaque, 0 = fully transparent

// Store linear distance light->fragment in [0,1] as depth, so the main pass can
// compare distances directly when sampling the cube. Also write a transmittance
// colour for translucent, coloured shadows.
void main() {
    gl_FragDepth = clamp(length(vWorld - uLightPos) / uFar, 0.0, 1.0);
    vec3 t = mix(vec3(1.0), uOccColor, uOpacity) * (1.0 - uOpacity);
    FragColor = vec4(t, 1.0);
}
