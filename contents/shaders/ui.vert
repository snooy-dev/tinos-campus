#version 330 core

uniform vec2 uWindow;
uniform vec4 uRect;
out vec2 uv;

void main()
{
    uv = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec2 position = (uRect.xy + uv * uRect.zw) / uWindow;
    gl_Position = vec4(position.x * 2.0 - 1.0, 1.0 - position.y * 2.0, 0.0, 1.0);
}
