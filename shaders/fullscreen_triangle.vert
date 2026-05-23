#version 460

layout(location = 0) out vec2 outUV;

void main()
{
    vec2 pos = vec2(
        (gl_VertexIndex == 2) ? 3.0 : -1.0,
        (gl_VertexIndex == 1) ? 3.0 : -1.0
    );

    outUV = pos * 0.5 + 0.5;

    gl_Position = vec4(pos, 0.0, 1.0);
}