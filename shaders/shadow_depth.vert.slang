struct Vertex
{
    float3 position;
    float uv_x;
    float3 normal;
    float uv_y;
    float4 tangent;
};

struct InstanceData
{
    float3 position;
    float pad0;
    float4 rotation;
    float3 scale;
    float pad1;
    float4 baseColorFactor;
    float4 flashColorAndAmount;
    float4 corruptionColorAndAmount;
    float4 corruptionParams;
};

struct PushConstants
{
    float4x4 lightViewProjection;
    uint64_t vertexBuffer;
    uint64_t instanceBuffer;
};

[[vk::push_constant]]
PushConstants pushConstants;

[shader("vertex")]
float4 main(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID) : SV_Position
{
    Ptr<Vertex> vertices = reinterpret<Ptr<Vertex>>(pushConstants.vertexBuffer);
    Ptr<InstanceData> instances = reinterpret<Ptr<InstanceData>>(pushConstants.instanceBuffer);
    Vertex vertex = vertices[vertexIndex];
    InstanceData instance = instances[instanceIndex];

    float3 position = vertex.position * instance.scale;
    float3 rotationIntermediate = 2.0 * cross(instance.rotation.xyz, position);
    position +=
        instance.rotation.w * rotationIntermediate +
        cross(instance.rotation.xyz, rotationIntermediate);
    position += instance.position;

    return mul(pushConstants.lightViewProjection, float4(position, 1.0));
}
