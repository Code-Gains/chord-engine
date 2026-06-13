#version 460

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inParams;
layout(location = 4) in vec3 inCameraPosition;

layout(location = 0) out vec4 outFragColor;

void main()
{
    float fresnelPower = max(inParams.x, 0.01);
    float fresnelIntensity = max(inParams.y, 0.0);
    float baseIntensity = max(inParams.z, 0.0);
    float alphaMultiplier = clamp(inParams.w, 0.0, 1.0);

    vec3 normal = normalize(inNormal);
    vec3 viewDir = normalize(inCameraPosition - inWorldPos);
    float ndotv = clamp(abs(dot(normal, viewDir)), 0.0, 1.0);
    float fresnel = pow(1.0 - ndotv, fresnelPower);

    vec3 color = inColor.rgb * (baseIntensity + fresnel * fresnelIntensity);
    float alpha = inColor.a * alphaMultiplier * clamp(0.45 + fresnel * 0.75, 0.0, 1.0);

    outFragColor = vec4(color, alpha);
}
