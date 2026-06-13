#version 460
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColor;
layout(location = 3) out vec4 outParams;
layout(location = 4) out vec3 outCameraPosition;

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 tangent;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant) uniform constants
{
    mat4 viewProjection;
    mat4 model;
    vec4 color;
    vec4 params;
    vec4 cameraPosition;
    VertexBuffer vertexBuffer;
} PushConstants;

void main()
{
    Vertex vertex = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
    vec4 worldPos = PushConstants.model * vec4(vertex.position, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.model)));

    gl_Position = PushConstants.viewProjection * worldPos;
    outWorldPos = worldPos.xyz;
    outNormal = normalize(normalMatrix * vertex.normal);
    outColor = PushConstants.color;
    outParams = PushConstants.params;
    outCameraPosition = PushConstants.cameraPosition.xyz;
}
