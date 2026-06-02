#version 460
#extension GL_EXT_buffer_reference : require

// layout(location = 0) out vec3 outColor;
// layout(location = 1) out vec2 outUV;

layout(location = 0) out vec3 outPos;
layout(location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outUV;
layout(location = 3) out vec4 outTangent;
//layout(location = 2)

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 tangent;
    //vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer { 
    Vertex vertices[];
};

struct InstanceData2 {
    mat4 model;
};
struct InstanceData {
    vec3 position;
    float pad0;
    vec4 rotation;   // quaternion xyzw
    vec3 scale;
    float pad1;
};

// struct InstanceData {
//     vec3 position;
//     vec4 rotation;
//     vec3 scale;
// };

layout(buffer_reference, std430) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(push_constant) uniform constants {
    mat4 viewProjection;
    VertexBuffer vertexBuffer;
    InstanceBuffer instanceBuffer;
} PushConstants;


mat4 buildTransform(vec3 pos, vec4 q, vec3 scale) {
    // Quaternion to mat3
    float x2 = q.x*q.x, y2 = q.y*q.y, z2 = q.z*q.z;
    float xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
    float wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;

    mat4 m = mat4(
        vec4(scale.x * (1.0 - 2.0*(y2+z2)),  scale.x * 2.0*(xy+wz),          scale.x * 2.0*(xz-wy),          0.0),
        vec4(scale.y * 2.0*(xy-wz),           scale.y * (1.0 - 2.0*(x2+z2)),  scale.y * 2.0*(yz+wx),          0.0),
        vec4(scale.z * 2.0*(xz+wy),           scale.z * 2.0*(yz-wx),          scale.z * (1.0 - 2.0*(x2+y2)),  0.0),
        vec4(pos,                                                                                                1.0)
    );
    return m;
}

vec3 applyTransform(vec3 point, vec3 pos, vec4 quat, vec3 scale){
  point *= scale;
  
  vec3 t = 2.0 * cross(quat.xyz, point);
  point += quat.w * t + cross(quat.xyz, t);
  return point + pos;
} 

vec3 rotateByQuat(vec3 v, vec4 q) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main() {
    Vertex vertex = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
    InstanceData inst = PushConstants.instanceBuffer.instances[gl_InstanceIndex]; // <-- use instance index

    // gl_Position = PushConstants.viewProjection * inst.model * vec4(v.position, 1.0);
    // mat4 model = buildTransform(inst.position, inst.rotation, inst.scale);
    // gl_Position = PushConstants.viewProjection * model * vec4(vertex.position, 1.0);
    vec3 worldPos = applyTransform(vertex.position, inst.position, inst.rotation, inst.scale);
    gl_Position = PushConstants.viewProjection * vec4(worldPos, 1.0);
    //outColor = vertex.normal;
    outPos = worldPos;
    //outNormal = applyTransform(vertex.normal, inst.position, inst.rotation, inst.scale);
    //outNormal = rotateByQuat(vertex.normal, inst.rotation);
    mat3 model3x3 = mat3(buildTransform(inst.position, inst.rotation, inst.scale));
    outNormal = normalize(transpose(inverse(model3x3)) * vertex.normal);
    outUV = vec2(vertex.uv_x, vertex.uv_y);
    outTangent = vec4(normalize(model3x3 * vertex.tangent.xyz), vertex.tangent.w);
}
