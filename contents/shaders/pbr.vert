#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord;

void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;
    vWorldNormal = uNormalMatrix * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * world;
}
