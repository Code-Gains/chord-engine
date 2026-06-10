#version 460

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec4 inBaseColorFactor;

layout(set = 0, binding = 0) uniform sampler2D colorTexture;
layout(set = 0, binding = 1) uniform sampler2D normalTexture;
layout(set = 0, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 3) uniform sampler2D occlusionTexture;
layout(set = 0, binding = 4) uniform sampler2D emissionTexture;

layout(set = 1, binding = 0) uniform SceneData {
    vec4 cameraPosition;
    vec4 sunlightDirection; // xyz = direction, w = intensity
    vec4 ambientColor;
    mat4 lightViewProjection;
} sceneData;

layout(set = 2, binding = 0) uniform samplerCube irradianceMap;
layout(set = 2, binding = 1) uniform samplerCube prefilteredMap;
layout(set = 2, binding = 2) uniform sampler2D brdfLUT;

layout(set = 3, binding = 0) uniform sampler2D shadowMap;

layout (location = 0) out vec4 outFragColor;

const float PI = 3.14159265359;
const float MIN_ROUGHNESS = 0.04;
const float IBL_INTENSITY = 0.25;
const float SHADOW_NORMAL_BIAS = 0.12;

vec3 fresnelSchlick(vec3 F0, vec3 F90, float VdotH)
{
    return F0 + (F90 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

float geometricOcclusion(float NdotL, float NdotV, float alphaRoughness)
{
    float r = alphaRoughness;

    float attenuationL =
        2.0 * NdotL /
        (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));

    float attenuationV =
        2.0 * NdotV /
        (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));

    return attenuationL * attenuationV;
}

float microfacetDistribution(float NdotH, float alphaRoughness)
{
    float roughnessSq = alphaRoughness * alphaRoughness;
    float f = (NdotH * roughnessSq - NdotH) * NdotH + 1.0;
    return roughnessSq / (PI * f * f);
}

vec3 getNormalFromMap()
{
    vec3 tangentNormal =
        texture(normalTexture, inUV).xyz * 2.0 - 1.0;

    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent.xyz);

    T = normalize(T - N * dot(N, T));

    vec3 B = cross(N, T) * inTangent.w;

    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

vec3 ACESFilm(vec3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;

    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float sampleShadow(vec3 normal, vec3 lightDirection)
{
    vec3 shadowPosition = inPos + normal * SHADOW_NORMAL_BIAS;
    vec4 lightClip = sceneData.lightViewProjection * vec4(shadowPosition, 1.0);
    vec3 lightNdc = lightClip.xyz / lightClip.w;
    vec2 shadowUv = lightNdc.xy * 0.5 + 0.5;

    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 ||
        shadowUv.y < 0.0 || shadowUv.y > 1.0 ||
        lightNdc.z < 0.0 || lightNdc.z > 1.0) {
        return 1.0;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float bias = max(0.00002, 0.00015 * (1.0 - clamp(dot(normal, lightDirection), 0.0, 1.0)));
    float lit = 0.0;

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float closestReversedDepth = texture(shadowMap, shadowUv + vec2(x, y) * texelSize).r;
            lit += lightNdc.z + bias >= closestReversedDepth ? 1.0 : 0.0;
        }
    }

    return lit / 9.0;
}

void main()
{
    // outFragColor = vec4(abs(inTangent.xyz), 1.0);
    // return;
    vec4 baseColor = texture(colorTexture, inUV) * inBaseColorFactor;

    vec3 normalSample = texture(normalTexture, inUV).xyz;

    vec4 metallicRoughness = texture(metallicRoughnessTexture, inUV);

    float roughness = metallicRoughness.g;
    float metallic = metallicRoughness.b;

    roughness = clamp(roughness, MIN_ROUGHNESS, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);

    float alphaRoughness = roughness * roughness;

    float ao = texture(occlusionTexture, inUV).r;

    vec3 emissive = texture(emissionTexture, inUV).rgb;

    // For now use mesh normal.
    // Later: use normalSample with TBN normal mapping.
    vec3 n = getNormalFromMap();
    //vec3 n = normalize(inNormal);

    // light/view directions.
    vec3 l = normalize(sceneData.sunlightDirection.xyz);
    vec3 v = normalize(sceneData.cameraPosition.xyz - inPos);
    vec3 h = normalize(l + v);

    float NdotL = max(dot(n, l), 0.0);
    float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
    float NdotH = clamp(dot(n, h), 0.0, 1.0);
    float VdotH = clamp(dot(v, h), 0.0, 1.0);

    vec3 f0 = vec3(0.04);
    vec3 diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
    diffuseColor *= 1.0 - metallic;

    vec3 specularColor = mix(f0, baseColor.rgb, metallic);

    float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
    float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);

    vec3 F = fresnelSchlick(
        specularColor,
        vec3(reflectance90),
        VdotH
    );

    float G = geometricOcclusion(NdotL, NdotV, alphaRoughness);
    float D = microfacetDistribution(NdotH, alphaRoughness);

    vec3 diffuseContrib = (1.0 - F) * (diffuseColor / PI);
    vec3 specContrib = F * G * D / max(4.0 * NdotL * NdotV, 0.001);

    // vec3 lightColor = vec3(3.0);
	// vec3 ambient = baseColor.rgb * 0.01;

    // vec3 lightColor = vec3(sceneData.sunlightDirection.w);
    // vec3 ambient = baseColor.rgb * 0.03;
    vec3 lightColor =
    sceneData.ambientColor.rgb *
    sceneData.sunlightDirection.w;

    vec3 ambient =
        baseColor.rgb *
        sceneData.ambientColor.rgb *
        sceneData.ambientColor.a;
        
    float shadow = NdotL > 0.0 ? sampleShadow(n, l) : 0.0;
    vec3 color = ambient + shadow * NdotL * lightColor * (diffuseContrib + specContrib);
	color *= ao;

    color += emissive;

    // -------------------------
    // SPECULAR IBL placeholder
    // -------------------------
    vec3 reflectionDir = reflect(-v, n);

    float maxReflectionLod = 8.0;
    float reflectionLod =
        roughness * roughness * maxReflectionLod;

    vec3 prefilteredColor =
        textureLod(
            prefilteredMap,
            reflectionDir,
            reflectionLod
        ).rgb;

    vec2 brdf =
        texture(
            brdfLUT,
            vec2(NdotV, roughness)
        ).rg;
    //brdf = vec2(0.5, 0.0);

    vec3 specularIBL =
        prefilteredColor *
        (F * brdf.x + brdf.y);

    color += specularIBL;

    // -------------------------
    // DIFFUSE IBL placeholder
    // -------------------------

    vec3 irradiance =
        texture(irradianceMap, n).rgb;

    vec3 diffuseIBL =
        irradiance * diffuseColor * ao;
    
    color += diffuseIBL * IBL_INTENSITY;

    // outFragColor = vec4(vec3(reflectionLod / maxReflectionLod), 1.0);
    // return;
    // outFragColor = vec4(environmentReflection * 5.0, 1.0);
    // return;
    // outFragColor = vec4(environmentReflection, 1.0);
    // return;

    // exposure
    float exposure = 1.0;
    color *= exposure;
    // tone mapping
    color = ACESFilm(color);
    // simple gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outFragColor = vec4(color, baseColor.a);
}
