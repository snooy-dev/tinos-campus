#version 330 core

in vec3 vWorldNormal;
in vec2 vTexCoord;

uniform vec4 uBaseColor = vec4(1.0);
uniform bool uUseBaseColorTexture = false;
uniform sampler2D uBaseColorTexture;
uniform bool uUseAlphaMask = false;
uniform sampler2D uAlphaMask;

uniform vec3 uAmbientLight = vec3(0.25);
uniform vec3 uLightColor = vec3(0.75);
// World-space direction from the surface toward the light.
uniform vec3 uLightDirection = vec3(0.4, 1.0, 0.3);

out vec4 fragColor;

void main()
{
    if (uUseAlphaMask && texture(uAlphaMask, vTexCoord).r < 0.5)
        discard;

    vec4 baseColor = uBaseColor;
    if (uUseBaseColorTexture)
        baseColor *= texture(uBaseColorTexture, vTexCoord);

    vec3 normal = normalize(vWorldNormal);
    // Leaf cards can be drawn from either side.
    if (!gl_FrontFacing)
        normal = -normal;

    float diffuse = max(dot(normal, normalize(uLightDirection)), 0.0);
    vec3 light = uAmbientLight + diffuse * uLightColor;
    fragColor = vec4(baseColor.rgb * light, baseColor.a);
}
