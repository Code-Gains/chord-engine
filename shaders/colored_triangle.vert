#version 460


//layout (location = 0) out vec3 outColor;
layout (location = 0) out vec3 outPos;
layout (location = 1) out vec3 outNormal;

void main() 
{
	//const array of positions for the triangle
	const vec3 positions[3] = vec3[3](
		vec3(1.0f, 1.0f, 0.0f), // bottom right
		vec3(-1.0f, 1.0f, 0.0f), // bottom left
		vec3(0.f,-1.0f, 0.0f) // top middle 
	);

	//const array of colors for the triangle
	const vec3 colors[3] = vec3[3](
		vec3(1.0f, 0.0f, 0.0f), //red
		vec3(0.0f, 1.0f, 0.0f), //green
		vec3(00.f, 0.0f, 1.0f)  //blue
	);

	//output the position of each vertex
	gl_Position = vec4(positions[gl_VertexIndex], 1.0f);
	//outColor = colors[gl_VertexIndex];
	outPos = vec3(0.0, 0.0, 0.0);
	outNormal = vec3(0.0, 0.0, 0.0);
}