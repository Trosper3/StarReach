#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform float time;

// --- Value noise ---

float hash(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i),                    hash(i + vec2(1.0, 0.0)), f.x),
        mix(hash(i + vec2(0.0, 1.0)),   hash(i + vec2(1.0, 1.0)), f.x),
        f.y
    );
}

// Fractional Brownian motion — 5 octaves, rotated each layer to break axis alignment
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    mat2  rot = mat2(0.8660, 0.5, -0.5, 0.8660);   // 30 degree rotation
    for (int i = 0; i < 5; i++) {
        v    += a * noise(p);
        p     = rot * p * 2.1 + vec2(1.7, 9.2);
        a    *= 0.5;
    }
    return v;
}

void main() {
    vec2  uv   = fragTexCoord * 2.0 - 1.0;   // remap 0..1 → -1..1
    float dist = length(uv);
    if (dist >= 1.0) discard;

    float t = time * 0.25;

    // Two levels of domain warping: each layer displaces the next, producing
    // the twisted organic plasma shapes rather than simple rounded blobs.
    vec2 q = vec2(
        fbm(uv * 2.0 + vec2(0.00, 0.00) + t * 0.30),
        fbm(uv * 2.0 + vec2(5.20, 1.30) + t * 0.25)
    );
    vec2 r = vec2(
        fbm(uv * 2.0 + 2.5 * q + vec2(1.70, 9.20) + t * 0.20),
        fbm(uv * 2.0 + 2.5 * q + vec2(8.30, 2.80) + t * 0.15)
    );

    float f = clamp(fbm(uv * 1.5 + 2.0 * r + t * 0.10), 0.0, 1.0);

    // Color ramp: deep red-orange → orange → yellow → hot white
    vec3 col = mix(vec3(0.48, 0.06, 0.00), vec3(0.85, 0.22, 0.00), smoothstep(0.00, 0.35, f));
    col      = mix(col, vec3(1.00, 0.45, 0.00),                     smoothstep(0.30, 0.55, f));
    col      = mix(col, vec3(1.00, 0.72, 0.05),                     smoothstep(0.50, 0.70, f));
    col      = mix(col, vec3(1.00, 0.95, 0.55),                     smoothstep(0.65, 0.88, f));
    col      = mix(col, vec3(1.00, 1.00, 0.95),                     smoothstep(0.82, 1.00, f));

    // Limb darkening — edges of the disk are cooler/dimmer, giving a spherical feel
    float limb = 1.0 - dist * dist * 0.55;
    col *= limb;

    // Soft circular edge so the disk dissolves rather than hard-clips
    float alpha = smoothstep(1.0, 0.90, dist);

    finalColor = vec4(col, alpha);
}
