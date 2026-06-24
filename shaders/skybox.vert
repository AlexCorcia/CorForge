#version 460 core

layout(location = 0) in vec3 aPos;

uniform mat4 uView; // rotation only (no translation)
uniform mat4 uProj;

out vec3 vDir;

void main() {
    vDir = aPos;
    vec4 p = uProj * uView * vec4(aPos, 1.0);
    gl_Position = p.xyww; // force depth = 1 (far plane), so the sky is behind all
}
