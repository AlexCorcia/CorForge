#version 460 core

// Fullscreen triangle generated from gl_VertexID -- no VBO needed. Draw with
// glDrawArrays(GL_TRIANGLES, 0, 3) and any VAO bound.
out vec2 vUV;

void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
