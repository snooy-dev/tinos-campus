#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;
uniform bool uHasSkin = false;
uniform mat4 uBones[32];
uniform mat4 uModel;
mat4 SkinMatrix()
{
    if (!uHasSkin) return mat4(1.0);
    ivec4 j=ivec4(aJoints);
    return aWeights.x*uBones[j.x]+aWeights.y*uBones[j.y]+aWeights.z*uBones[j.z]+aWeights.w*uBones[j.w];
}
uniform mat4 uViewProjection;
uniform mat3 uNormalMatrix;
uniform mat4 uLightViewProjection;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec4 vLightPosition;

void main()
{
    mat4 skin = SkinMatrix();
    vec4 world = uModel * skin * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;
    vWorldNormal = uNormalMatrix * transpose(inverse(mat3(skin))) * aNormal;
    vTexCoord = aTexCoord;
    vLightPosition = uLightViewProjection * world;
    gl_Position = uViewProjection * world;
}
