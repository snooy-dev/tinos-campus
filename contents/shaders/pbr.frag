#version 330 core

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vTexCoord;

uniform vec4 uCamera;
uniform vec4 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAlphaCutoff;
uniform bool uHasNormals;
uniform bool uHasBaseTexture;
uniform bool uHasMetallicRoughnessTexture;
uniform sampler2D uBaseTexture;
uniform sampler2D uMetallicRoughnessTexture;

out vec4 fragColor;
const float pi = 3.14159265;

vec3 unitVector(vec3 value)
{
    return value * inversesqrt(max(dot(value, value), 1e-12));
}

vec3 toSrgb(vec3 linear)
{
    return mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055,
        greaterThan(linear, vec3(0.0031308)));
}

void main()
{
    vec3 faceNormal = cross(dFdx(vWorldPosition), dFdy(vWorldPosition));
    vec3 normal = uHasNormals ? vWorldNormal : faceNormal;
    if (uHasNormals && !gl_FrontFacing)
        normal = -normal;
    normal = unitVector(normal);

    vec4 base = uBaseColor;
    if (uHasBaseTexture)
        base *= texture(uBaseTexture, vTexCoord);
    vec2 metalRough = vec2(uMetallic, uRoughness);
    if (uHasMetallicRoughnessTexture)
        metalRough *= texture(uMetallicRoughnessTexture, vTexCoord).bg;
    if (base.a < uAlphaCutoff)
        discard;

    float metallic = clamp(metalRough.x, 0.0, 1.0);
    float roughness = clamp(metalRough.y, 0.06, 1.0);
    vec3 light = normalize(vec3(0.4, 1.0, 0.6));
    vec3 view = unitVector(uCamera.xyz - uCamera.w * vWorldPosition);
    vec3 halfway = unitVector(light + view);
    float noL = max(dot(normal, light), 0.0);
    float noV = max(dot(normal, view), 0.0);
    float noH = clamp(dot(normal, halfway), 0.0, 1.0);
    float voH = clamp(dot(view, halfway), 0.0, 1.0);

    vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - voH, 5.0);
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denominator = (1.0 - noH * noH) + noH * noH * alpha2;
    float distribution = alpha2 / (pi * denominator * denominator);
    float visibility = 0.5 / max(
        noL * sqrt(noV * noV * (1.0 - alpha2) + alpha2) +
        noV * sqrt(noL * noL * (1.0 - alpha2) + alpha2), 1e-6);
    vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base.rgb / pi;
    vec3 specular = distribution * visibility * fresnel;

    vec3 color = (diffuse + specular) * (2.6 * noL) + 0.16 * mix(base.rgb, f0, metallic);
    fragColor = vec4(toSrgb(clamp(color, 0.0, 1.0)), 1.0);
}
