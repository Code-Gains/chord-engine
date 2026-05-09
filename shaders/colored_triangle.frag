#version 460

//shader input
//layout (location = 0) in vec3 inColor;

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
//layout (location = 2) out vec3 inColor;
//layout (location = 2) in vec2 inUV;

//output write
layout (location = 0) out vec4 outFragColor;

void main() 
{
	//return red
	//outFragColor = vec4(inColor,1.0f);
	//outFragColor = vec4(inColor * 0.5 + 0.5, 1.0);
	// vec3 n = inColor * 0.5 + 0.5;
	// outFragColor = vec4(n, 1.0);

	// float intensity = 1.0 - length(inColor); // 0 at edges, 1 at center
	// outFragColor = vec4(vec3(intensity), 1.0);
	// float dist = length(inPos);
	// float intensity = 100.0 / (1.0 + dist); // smooth falloff

	// outFragColor = vec4(vec3(intensity), 1.0);

	// vec3 p = inPos;

    // // --- distance-based glow ---
    // float dist = length(p);
    // float intensity = exp(-dist * 0.005); // tweak this

    // // --- direction-based color (handles negative nicely) ---
    // vec3 dirColor = abs(normalize(p));

    // // --- soft base color tint (purple/blue space vibe) ---
    // vec3 baseColor = mix(vec3(0.2, 0.3, 0.8), vec3(0.8, 0.3, 1.0), dirColor);

    // // --- simple lighting from normals ---
    // vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    // float lighting = max(dot(normalize(inNormal), lightDir), 0.0);

    // // --- combine ---
    // vec3 color = baseColor * intensity;
    // color += vec3(1.0) * intensity * 0.5; // glow boost
    // color *= 0.5 + 0.5 * lighting;        // soft shading

    // outFragColor = vec4(color, 1.0);

	// vec3 dir = normalize(inPos);

	// // choose a direction (can tweak this!)
	// vec3 ref = normalize(vec3(1.0, 1.0, 1.0));

	// float d = dot(dir, ref);        // [-1, 1]
	// float colorVal = d * 0.5 + 0.5; // [0, 1]

	// outFragColor = vec4(vec3(colorVal), 1.0);
	vec3 lightDir = normalize(-inPos); // from fragment to center
	vec3 N = normalize(inNormal); // world normal
	float ambient = 0.005; // small light for shadows
	float diffuse = max(dot(N, lightDir), 0.0); // standard Lambert
	float distance = length(inPos);
	float attenuation = 1.0 / (1.0 + 0.00001 * distance * distance);
	//float attenuation = 1.0 / (1.0 + 0.0001 * distance * distance);
	float intensity = (ambient + diffuse) * attenuation;
	outFragColor = vec4(vec3(intensity), 1.0);


}