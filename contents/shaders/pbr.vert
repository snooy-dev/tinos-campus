#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;
uniform bool uHasSkin = false;
uniform mat4 uBones[32];
uniform mat4 uModel;
uniform samplerBuffer uMorphDeltas;
uniform int uMorphCount;
uniform int uMorphVertexCount;
uniform float uMorphWeights[32];

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
    vec3 position = aPosition;
    vec3 normal = aNormal;
    for (int target = 0; target < uMorphCount; ++target)
    {
        float weight = uMorphWeights[target];
        if (weight == 0.0) continue;
        int texel = (target * uMorphVertexCount + gl_VertexID) * 2;
        position += weight * texelFetch(uMorphDeltas, texel).xyz;
        normal += weight * texelFetch(uMorphDeltas, texel + 1).xyz;
    }
    mat4 skin = SkinMatrix();
    vec4 world = uModel * skin * vec4(position, 1.0);
    vWorldPosition = world.xyz;
    vWorldNormal = uNormalMatrix * transpose(inverse(mat3(skin))) * normal;
    vTexCoord = aTexCoord;
    vLightPosition = uLightViewProjection * world;
    gl_Position = uViewProjection * world;
}
