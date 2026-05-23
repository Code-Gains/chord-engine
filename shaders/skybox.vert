#version 460

layout(location = 0) out vec3 outDirection;

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
} pc;

vec3 positions[36] = vec3[](
    vec3(-1,-1,-1), vec3(-1, 1,-1), vec3( 1, 1,-1),
    vec3( 1, 1,-1), vec3( 1,-1,-1), vec3(-1,-1,-1),

    vec3(-1,-1, 1), vec3( 1, 1, 1), vec3(-1, 1, 1),
    vec3( 1, 1, 1), vec3(-1,-1, 1), vec3( 1,-1, 1),

    vec3(-1, 1, 1), vec3( 1, 1, 1), vec3( 1, 1,-1),
    vec3( 1, 1,-1), vec3(-1, 1,-1), vec3(-1, 1, 1),

    vec3(-1,-1, 1), vec3(-1,-1,-1), vec3( 1,-1,-1),
    vec3( 1,-1,-1), vec3( 1,-1, 1), vec3(-1,-1, 1),

    vec3( 1,-1, 1), vec3( 1,-1,-1), vec3( 1, 1,-1),
    vec3( 1, 1,-1), vec3( 1, 1, 1), vec3( 1,-1, 1),

    vec3(-1,-1, 1), vec3(-1, 1,-1), vec3(-1,-1,-1),
    vec3(-1, 1,-1), vec3(-1,-1, 1), vec3(-1, 1, 1)
);

void main()
{
    vec3 pos = positions[gl_VertexIndex];

    outDirection = pos;

    vec4 clipPos = pc.viewProjection * vec4(pos, 1.0);

    gl_Position = clipPos.xyww;
}