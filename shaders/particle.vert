#version 460 core

// Instanced camera-facing billboards. A single unit quad (aCorner) is expanded
// per particle using the view matrix's right/up axes, so every sprite always
// faces the camera regardless of where it sits.
layout(location = 0) in vec2 aCorner; // -0.5..0.5
layout(location = 1) in vec3 aPos;    // instance world position
layout(location = 2) in float aSize;  // instance world size
layout(location = 3) in vec4 aColor;  // instance rgba (rgb may be HDR > 1)
layout(location = 4) in vec3 aVel;    // instance velocity (for stretched streaks)

uniform mat4  uView;
uniform mat4  uProj;
uniform float uStretch; // 0 = round billboard; >0 = elongate along velocity

out vec2 vUV;
out vec4 vColor;

void main() {
    vec3 camRight = vec3(uView[0][0], uView[1][0], uView[2][0]);
    vec3 camUp    = vec3(uView[0][1], uView[1][1], uView[2][1]);

    vec3 world;
    float speed = length(aVel);
    if (uStretch > 0.0 && speed > 0.001) {
        // Streak: long axis = world velocity (faces the camera), thin perpendicular.
        vec3 camFwd = vec3(uView[0][2], uView[1][2], uView[2][2]);
        vec3 along  = aVel / speed;
        vec3 side   = cross(along, camFwd);
        float sl = length(side);
        side = sl > 1e-4 ? side / sl : camRight;
        float longLen = aSize * (1.0 + uStretch * speed);
        world = aPos + side * (aCorner.x * aSize) + along * (aCorner.y * longLen);
    } else {
        world = aPos + (camRight * aCorner.x + camUp * aCorner.y) * aSize;
    }

    vUV    = aCorner + 0.5;
    vColor = aColor;
    gl_Position = uProj * uView * vec4(world, 1.0);
}
