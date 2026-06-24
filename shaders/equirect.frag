#version 460 core

// Equirectangular image -> one cubemap face (sample by direction).
in vec3 vDir;
out vec4 FragColor;

uniform sampler2D uEquirect;

const vec2 invAtan = vec2(0.1591, 0.3183); // 1/(2pi), 1/pi

void main() {
    vec3 d = normalize(vDir);
    vec2 uv = vec2(atan(d.z, d.x), asin(clamp(d.y, -1.0, 1.0))) * invAtan + 0.5;
    FragColor = vec4(texture(uEquirect, uv).rgb, 1.0);
}
