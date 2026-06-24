#version 460 core

// Soft round sprite. Optionally "soft particles": fade out as the sprite nears
// opaque geometry (read from the resolved scene depth) so spray melts into the
// water/ground instead of showing a hard intersection line. Writes linear HDR --
// the post composite tonemaps, and bright (>1) colours feed the bloom.
in vec2 vUV;
in vec4 vColor;
out vec4 frag;

uniform sampler2D uDepthTex;
uniform vec2  uScreenSize;
uniform float uNear;
uniform float uFar;
uniform int   uSoft;

float linearize(float d) {
    float z = d * 2.0 - 1.0;
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

void main() {
    vec2 p = vUV * 2.0 - 1.0;
    float a = smoothstep(1.0, 0.15, dot(p, p)) * vColor.a; // soft circular falloff

    if (uSoft == 1) {
        vec2 suv = gl_FragCoord.xy / uScreenSize;
        float sceneZ = linearize(texture(uDepthTex, suv).r);
        float fragZ  = linearize(gl_FragCoord.z);
        a *= clamp((sceneZ - fragZ) / 0.5, 0.0, 1.0);
    }

    if (a <= 0.003) discard;
    frag = vec4(vColor.rgb, a);
}
