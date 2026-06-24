#version 460 core

// Renders a unit cube; the local position doubles as the sample direction. Used
// to capture the environment into the 6 faces of a cubemap.
layout(location = 0) in vec3 aPos;

uniform mat4 uView; // rotation only (camera at origin)
uniform mat4 uProj;

out vec3 vDir;

void main() {
    vDir = aPos;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
