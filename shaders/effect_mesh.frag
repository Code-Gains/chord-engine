#version 460

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inParams;
layout(location = 4) in vec3 inCameraPosition;
layout(location = 5) in vec4 inCorruptionColor;
layout(location = 6) in vec4 inCorruptionParams;
layout(location = 7) in float inEffectAge;

layout(location = 0) out vec4 outFragColor;

float Hash13(vec3 value)
{
    value = fract(value * 0.1031);
    value += dot(value, value.yzx + 33.33);
    return fract((value.x + value.y) * value.z);
}

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

    float corruptionScale = max(inCorruptionParams.x, 0.001);
    float corruptionSoftness = clamp(inCorruptionParams.y, 0.001, 1.0);
    float corruptionIntensity = max(inCorruptionParams.z, 0.0);
    float corruptionAmount = clamp(inCorruptionParams.w, 0.0, 1.0);

    float baseStep = floor(inEffectAge * 17.0);
    float holdNoise = Hash13(vec3(baseStep * 0.37, 21.0, 5.0));
    float holdLength = mix(1.0, 4.0, step(0.55, holdNoise));
    float glitchStep = floor(baseStep / holdLength);
    float modeNoise = Hash13(vec3(glitchStep, 31.0, 2.0));
    float coverageNoise = Hash13(vec3(glitchStep, 41.0, 9.0));
    float blinkNoise = Hash13(vec3(glitchStep, 53.0, 4.0));
    float smallCutPhase = step(0.46, modeNoise);
    float blinkPhase = step(0.73, blinkNoise);

    float scaleMode = mix(
        mix(0.14, 0.62, Hash13(vec3(glitchStep, 61.0, 1.0))),
        mix(2.2, 5.6, Hash13(vec3(glitchStep, 67.0, 3.0))),
        smallCutPhase);
    vec3 glitchOffset = vec3(
        Hash13(vec3(glitchStep, 1.0, 7.0)),
        Hash13(vec3(glitchStep, 2.0, 8.0)),
        Hash13(vec3(glitchStep, 3.0, 9.0))) * mix(9.0, 41.0, smallCutPhase);
    vec3 cellPosition = inWorldPos * corruptionScale * scaleMode + glitchOffset;
    vec3 cell = floor(cellPosition);
    float cellNoise = Hash13(cell + floor(normal * 7.0));
    float detailNoise = Hash13(floor(cellPosition * 2.0) + normal * 19.0);
    float stripeNoise = step(
        mix(0.78, 0.55, smallCutPhase),
        fract(dot(floor(cellPosition), vec3(0.17, 0.31, 0.43)) + glitchStep * 0.37));
    float corruptionNoise = mix(cellNoise, detailNoise, 0.35);
    corruptionNoise = mix(corruptionNoise, stripeNoise, mix(0.12, 0.34, smallCutPhase));
    float amountPulse = mix(0.48, 1.82, coverageNoise);
    amountPulse *= mix(1.15, 0.62, smallCutPhase);
    amountPulse = mix(amountPulse, mix(0.0, 0.22, Hash13(vec3(glitchStep, 71.0, 6.0))), blinkPhase);
    float threshold = 1.0 - clamp(corruptionAmount * amountPulse, 0.0, 1.0);
    float corruptionMask = smoothstep(
        threshold - corruptionSoftness,
        threshold + corruptionSoftness,
        corruptionNoise);

    vec3 faceColor = inColor.rgb * (baseIntensity + fresnel * fresnelIntensity);
    vec3 corruptionColor = inCorruptionColor.rgb * (baseIntensity + fresnel * fresnelIntensity + corruptionIntensity);
    vec3 color = mix(faceColor, corruptionColor, corruptionMask * inCorruptionColor.a);
    float alpha = inColor.a * alphaMultiplier * clamp(0.45 + fresnel * 0.75, 0.0, 1.0);
    alpha = max(alpha, inCorruptionColor.a * corruptionMask * alphaMultiplier);

    outFragColor = vec4(color, alpha);
}
