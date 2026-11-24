#version 330 core

in vec3 vSkyDirection;

uniform vec3 uHorizonColor;
uniform vec3 uZenithColor;

out vec4 fragColor;

vec3 toSrgb(vec3 linear)
{
    return mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055,
        greaterThan(linear, vec3(0.0031308)));
}

void main()
{
    float height = normalize(vSkyDirection).y;
    float blend = smoothstep(0.0, 1.0, height);
    vec3 color = mix(uHorizonColor, uZenithColor, blend);
    fragColor = vec4(toSrgb(clamp(color, 0.0, 1.0)), 1.0);
}
