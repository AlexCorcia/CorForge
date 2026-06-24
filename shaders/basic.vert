#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMatrix;
uniform vec4 uClipPlane; // world-space (n, d); only active when GL_CLIP_DISTANCE0 is enabled

out vec3 vNormal;
out vec3 vTangent;
out vec3 vFragPos;
out vec2 vUV;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vFragPos = world.xyz;
    vNormal  = uNormalMatrix * aNormal;
    vTangent = uNormalMatrix * aTangent;
    vUV      = aUV;
    gl_ClipDistance[0] = dot(world, uClipPlane);
    gl_Position = uProj * uView * world;
}
