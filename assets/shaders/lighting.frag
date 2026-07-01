#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float lightIntensity; // 0 = far/no sun, 1 = adjacent to sun

const float AMBIENT = 0.18;

void main() {
    vec4 tex = texture(texture0, fragTexCoord);

    // Every reachable pixel gets the same brightness — proximity to the star only
    float brightness = AMBIENT + (1.0 - AMBIENT) * lightIntensity;

    gl_FragColor = vec4(tex.rgb * fragColor.rgb * brightness, tex.a * fragColor.a);
}
