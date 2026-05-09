#version 460
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec3 outPos;
layout(location = 1) out vec3 outNormal;
//layout(location = 2) out vec2 outUV;

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant) uniform constants
{
    mat4 viewProjection;
    mat4 model;
    VertexBuffer vertexBuffer;
} PushConstants;

void main()
{
    Vertex vertex = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

    vec4 worldPos = PushConstants.model * vec4(vertex.position, 1.0);

    gl_Position = PushConstants.viewProjection * worldPos;
    outPos = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.model)));
    outNormal = normalize(normalMatrix * vertex.normal);

    //outUV = vec2(vertex.uv_x, vertex.uv_y);
}

// #version 460
// #extension GL_EXT_buffer_reference : require

// layout(location = 0) out vec3 outPos;
// layout(location = 1) out vec3 outNormal;
// //layout (location = 2) out vec3 outColor;
// layout (location = 2) out vec2 outUV;

// struct Vertex {
// 	vec3 position;
// 	float uv_x;
// 	vec3 normal;
// 	float uv_y;
// }; 

// layout(buffer_reference, std430) readonly buffer VertexBuffer{ 
// 	Vertex vertices[];
// };

// //push constants block
// layout( push_constant ) uniform constants
// {	
// 	mat4 render_matrix;
// 	VertexBuffer vertexBuffer;
// } PushConstants;

// void main() 
// {	
// 	//load vertex data from device adress
// 	Vertex vertex = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

// 	//output data
// 	gl_Position = PushConstants.render_matrix * vec4(vertex.position, 1.0f);
// 	outPos = vertex.position;
// 	outNormal = vertex.normal;
// 	//outColor = vertex.normal;
// 	outUV.x = vertex.uv_x;
// 	outUV.y = vertex.uv_y;
// }