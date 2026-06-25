#version 460 core

// Screen-space reflections (depth-only, no G-buffer). Reconstructs view-space
// position + normal from the depth buffer, marches the reflection ray through the
// depth buffer, and adds the hit scene colour back -- weighted by a Fresnel term
// so reflections concentrate at grazing angles (wet-floor look) and fade at screen
// edges. Approximate by nature: misses off-screen geometry, opt-in via a slider.
in vec2 vUV;
out vec4 frag;

uniform sampler2D uScene;   // linear HDR (lit scene)
uniform sampler2D uDepth;   // resolved scene depth
uniform vec2      uTexel;
uniform mat4      uProj;
uniform mat4      uInvProj;
uniform float     uNear;
uniform float     uFar;
uniform float     uIntensity;
uniform int       uMaxSteps;
uniform float     uMaxDist;   // view-space ray length
uniform float     uThickness; // depth-test tolerance (view units)

vec3 viewPos(vec2 uv) {
    float d = texture(uDepth, uv).r;
    vec4 ndc = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = uInvProj * ndc;
    return v.xyz / v.w;
}

void main() {
    vec3 base = texture(uScene, vUV).rgb;
    float d = texture(uDepth, vUV).r;
    if (d >= 0.9999) { frag = vec4(base, 1.0); return; } // sky: nothing to reflect

    vec3 P = viewPos(vUV);
    // View-space normal from position derivatives, oriented toward the camera.
    vec3 dx = viewPos(vUV + vec2(uTexel.x, 0.0)) - P;
    vec3 dy = viewPos(vUV + vec2(0.0, uTexel.y)) - P;
    vec3 N = normalize(cross(dx, dy));
    if (dot(N, normalize(-P)) < 0.0) N = -N;

    vec3 V = normalize(P);          // camera -> surface
    vec3 R = reflect(V, N);         // reflection direction (view space)

    float stepLen = uMaxDist / float(uMaxSteps);
    vec3 ray = P;
    float hitAmt = 0.0;
    vec3 hitColor = vec3(0.0);
    for (int i = 0; i < uMaxSteps; ++i) {
        ray += R * stepLen;
        if (ray.z > -uNear) break;                 // crossed behind the near plane
        vec4 clip = uProj * vec4(ray, 1.0);
        vec2 suv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;
        float sceneZ = viewPos(suv).z;             // view-space z of geometry there
        // Hit when the ray passes just behind the stored surface.
        if (ray.z < sceneZ - 0.02 && ray.z > sceneZ - uThickness) {
            vec2 e = smoothstep(0.0, 0.12, suv) * (1.0 - smoothstep(0.88, 1.0, suv));
            float edge = e.x * e.y;                // fade near screen borders
            float fres = pow(1.0 - max(dot(-V, N), 0.0), 3.0); // grazing reflects more
            hitAmt = edge * fres;
            hitColor = texture(uScene, suv).rgb;
            break;
        }
    }
    frag = vec4(base + hitColor * (hitAmt * uIntensity), 1.0);
}
