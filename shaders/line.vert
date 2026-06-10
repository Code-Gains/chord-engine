#version 460
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec4 outColor;

struct LineVertex {
    vec3 position;
    float pad0;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer LineVertexBuffer {
    LineVertex vertices[];
};

layout(push_constant) uniform constants {
    mat4 viewProjection;
    LineVertexBuffer vertexBuffer;
} PushConstants;

void main()
{
    LineVertex vertex = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
    gl_Position = PushConstants.viewProjection * vec4(vertex.position, 1.0);
    outColor = vertex.color;
}
