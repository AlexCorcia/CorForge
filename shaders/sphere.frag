#version 460 core

// Sphere map (mirror-ball, ~1:1: a mirrored sphere photographed from +Z) -> one
// cubemap face. Standard spherical environment mapping; the centre of the ball is
// +Z and the rim wraps to -Z (so the back hemisphere is compressed/singular).
in vec3 vDir;
out vec4 FragColor;

uniform sampler2D uSphere;

void main() {
    vec3 d = normalize(vDir);
    float m = 2.0 * sqrt(d.x * d.x + d.y * d.y + (d.z + 1.0) * (d.z + 1.0));
    vec2 uv = d.xy / m + 0.5;
    // Source loaded vertically flipped (v = 0 bottom); the ball image origin is
    // top, so flip v back.
    FragColor = vec4(textureLod(uSphere, vec2(uv.x, 1.0 - uv.y), 0.0).rgb, 1.0);
}
