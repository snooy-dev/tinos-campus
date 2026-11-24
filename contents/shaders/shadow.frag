#version 330 core

in vec2 vTexCoord;

uniform float uBaseAlpha;
uniform float uAlphaCutoff;
uniform bool uHasBaseTexture;
uniform sampler2D uBaseTexture;

void main()
{
    float alpha = uBaseAlpha;
    if (uHasBaseTexture)
        alpha *= texture(uBaseTexture, vTexCoord).a;
    if (alpha < uAlphaCutoff)
        discard;
}
