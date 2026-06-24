#version 460 core

layout(location = 0) in vec3 aPos;

uniform mat4 uModel;
uniform mat4 uFaceVP; // face projection * view for one cube face

out vec3 vWorld;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    gl_Position = uFaceVP * world;
}
