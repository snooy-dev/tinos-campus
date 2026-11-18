#version 330 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uSkyViewProjection;

out vec3 vSkyDirection;

void main()
{
    vSkyDirection = aPosition;
    vec4 position = uSkyViewProjection * vec4(aPosition, 1.0);
    gl_Position = position.xyww;
}
