#version 460 core

// Procedural sky gradient -> one cubemap face. Stored linear (HDR-ready).
in vec3 vDir;
out vec4 FragColor;

uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uGround;

void main() {
    vec3 d = normalize(vDir);
    vec3 c = (d.y >= 0.0)
        ? mix(uHorizon, uZenith, pow(clamp(d.y, 0.0, 1.0), 0.5))
        : mix(uHorizon, uGround, clamp(-d.y * 4.0, 0.0, 1.0));
    FragColor = vec4(c, 1.0);
}
