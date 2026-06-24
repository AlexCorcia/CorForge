#version 460 core

// Depth pass also writes a transmittance colour: white = light passes (fully
// transparent), black = opaque blocks, tinted = coloured glass. Depth-tested, so
// the nearest occluder's transmittance wins.
out vec4 FragColor;

uniform vec3  uOccColor; // occluder albedo (the tint)
uniform float uOpacity;  // 1 = opaque, 0 = fully transparent

void main() {
    vec3 t = mix(vec3(1.0), uOccColor, uOpacity) * (1.0 - uOpacity);
    FragColor = vec4(t, 1.0);
}
