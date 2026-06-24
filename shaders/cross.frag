#version 460 core

// Horizontal-cross cubemap image (4 wide x 3 tall) -> one cubemap face.
// Layout (image space, row 0 = top):
//          [ +Y ]
//   [ -X ][ +Z ][ +X ][ -Z ]
//          [ -Y ]
// The source texture was loaded vertically flipped (v = 0 is the bottom row).
in vec3 vDir;
out vec4 FragColor;

uniform sampler2D uCross;

void main() {
    vec3 d = normalize(vDir);
    vec3 a = abs(d);
    vec2 uv;    // face uv in [-1,1], +x right, +y up
    vec2 cell;  // (col, row) in image space (row 0 = top)
    float ma;
    if (a.x >= a.y && a.x >= a.z) {
        ma = a.x;
        if (d.x >= 0.0) { uv = vec2(-d.z, -d.y); cell = vec2(2.0, 1.0); } // +X
        else            { uv = vec2( d.z, -d.y); cell = vec2(0.0, 1.0); } // -X
    } else if (a.y >= a.z) {
        ma = a.y;
        if (d.y >= 0.0) { uv = vec2( d.x,  d.z); cell = vec2(1.0, 0.0); } // +Y
        else            { uv = vec2( d.x, -d.z); cell = vec2(1.0, 2.0); } // -Y
    } else {
        ma = a.z;
        if (d.z >= 0.0) { uv = vec2( d.x, -d.y); cell = vec2(1.0, 1.0); } // +Z
        else            { uv = vec2(-d.x, -d.y); cell = vec2(3.0, 1.0); } // -Z
    }
    uv = uv / ma * 0.5 + 0.5; // -> [0,1], y up

    // Position inside the cross in image space (row 0 at top), then flip v for the
    // vertically-flipped texture.
    float cu = (cell.x + uv.x) / 4.0;
    float imgV = (cell.y + (1.0 - uv.y)) / 3.0; // y up within the cell -> image y down
    FragColor = vec4(textureLod(uCross, vec2(cu, 1.0 - imgV), 0.0).rgb, 1.0);
}
