#version 330 core

in vec2 vTexCoord;

uniform vec3 uEmissiveColor = vec3(1.0);
uniform float uEmissiveIntensity = 0.0;
uniform bool uHasEmissiveTexture = false;
uniform sampler2D uEmissiveTexture;
uniform bool uGlowPass = false;
uniform float uGlowOpacity = 0.22;

out vec4 fragColor;

vec3 toSrgb(vec3 linear)
{
    return mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055,
        greaterThan(linear, vec3(0.0031308)));
}

void main()
{
    if (uGlowPass)
    {
        vec2 offset = vTexCoord * 2.0 - 1.0;
        float distanceSquared = dot(offset, offset);
        float fade = 1.0 - smoothstep(0.0, 1.0, distanceSquared);
        float opacity = clamp(uGlowOpacity * uEmissiveIntensity, 0.0, 1.0);
        fragColor = vec4(toSrgb(clamp(uEmissiveColor, 0.0, 1.0)), fade * fade * opacity);
        return;
    }

    vec3 emission = uEmissiveColor * max(uEmissiveIntensity, 0.0);
    if (uHasEmissiveTexture)
        emission *= texture(uEmissiveTexture, vTexCoord).rgb;

    fragColor = vec4(toSrgb(clamp(emission, 0.0, 1.0)), 1.0);
}
