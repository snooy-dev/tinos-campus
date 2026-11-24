#version 330 core

in vec2 uv;
uniform sampler2D uTexture;
uniform bool uHasTexture;
uniform vec4 uColor;
out vec4 color;

void main()
{
    vec4 sampleColor = uHasTexture ? texture(uTexture, uv) : vec4(1.0);
    color = sampleColor * vec4(uColor.rgb * uColor.a, uColor.a);
}
