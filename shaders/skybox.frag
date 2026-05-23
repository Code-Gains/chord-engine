#version 460

layout(location = 0) in vec3 inDirection;

layout(set = 0, binding = 0) uniform samplerCube skyboxTexture;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 dir = normalize(inDirection);
    dir.z *= -1.0;
    vec3 color = texture(skyboxTexture, normalize(inDirection)).rgb;
    outColor = vec4(color, 1.0);

    // vec3 dir = normalize(inDirection);
    // outColor = vec4(abs(dir), 1.0);
}