#version 460 core

in vec3 vDir;
out vec4 FragColor;

uniform samplerCube uEnv;
uniform int uApplyGamma;
uniform int uApplyFog;   // reflection captures: haze the sky toward the horizon
uniform vec3 uFogColor;

void main() {
    vec3 dir = normalize(vDir);
    vec3 c = texture(uEnv, dir).rgb;
    // In reflection captures the main view's post fog is absent, so fade the sky
    // toward the fog colour near/below the horizon to match the foggy scene.
    if (uApplyFog == 1) {
        float band = exp(-max(dir.y, 0.0) * 1.3); // 1 at horizon, fades upward
        c = mix(c, uFogColor, clamp(band, 0.0, 1.0));
    }
    if (uApplyGamma == 1) {        // final screen pass: tonemap + gamma once
        c = c / (c + vec3(1.0));
        c = pow(c, vec3(1.0 / 2.2));
    }
    FragColor = vec4(c, 1.0);
}
