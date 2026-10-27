// Optional forward-material paint. Stamps are culled by a CPU-built world-space hash grid.
[[vk::binding(1, 1)]] StructuredBuffer<uint4> surfacePaintData;

uint SurfacePaintHash(int3 cell)
{
    uint3 c = uint3(cell);
    return (c.x * 73856093u ^ c.y * 19349663u ^ c.z * 83492791u) & 1023u;
}

float SurfacePaintNoiseHash(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float4 SurfacePaintNoise(float3 p)
{
    float3 cell = floor(p), f = frac(p);
    float3 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float3 dw = 30.0 * f * f * (f - 1.0) * (f - 1.0);
    float a = SurfacePaintNoiseHash(cell), b = SurfacePaintNoiseHash(cell + float3(1, 0, 0));
    float c = SurfacePaintNoiseHash(cell + float3(0, 1, 0)), d = SurfacePaintNoiseHash(cell + float3(1, 1, 0));
    float e = SurfacePaintNoiseHash(cell + float3(0, 0, 1)), g = SurfacePaintNoiseHash(cell + float3(1, 0, 1));
    float h = SurfacePaintNoiseHash(cell + float3(0, 1, 1)), j = SurfacePaintNoiseHash(cell + float3(1, 1, 1));
    float lower = lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
    float upper = lerp(lerp(e, g, w.x), lerp(h, j, w.x), w.y);
    float dx = lerp(lerp(b - a, d - c, w.y), lerp(g - e, j - h, w.y), w.z) * dw.x;
    float dy = lerp(lerp(c - a, d - b, w.x), lerp(h - e, j - g, w.x), w.z) * dw.y;
    return float4(lerp(lower, upper, w.z), dx, dy, (upper - lower) * dw.z);
}

float ApplySurfacePaint(float3 position, float3 geometricNormal, uint receiver,
                       inout float3 color, inout float roughness, inout float metallic, inout float3 normal)
{
    uint4 header = surfacePaintData[0];
    if (header.x == 0 || receiver == 0) return 0.0;
    float3 positionDx = ddx(position), positionDy = ddy(position);
    uint bucket = SurfacePaintHash(int3(floor(position / asfloat(header.y))));
    uint2 range = surfacePaintData[1 + bucket].xy;
    float coverage = 0.0;
    float ribbonCoverage = 0.0;
    for (uint i = 0; i < range.y; ++i)
    {
        uint index = surfacePaintData[range.x + i].x;
        uint record = header.z + index * 7;
        uint3 identity = surfacePaintData[record + 5].xyz;
        if (identity.x != receiver) continue;
        float4 center = asfloat(surfacePaintData[record]);
        float4 direction = asfloat(surfacePaintData[record + 1]);
        float3 offset = position - center.xyz;
        if (identity.z != 0)
        {
            float3 axis = asfloat(surfacePaintData[record + 6]).xyz - center.xyz;
            float t = saturate(dot(offset, axis) / max(dot(axis, axis), 0.000001));
            offset -= axis * t;
        }
        float height = dot(offset, direction.xyz);
        if (abs(height) > direction.w || dot(geometricNormal, direction.xyz) < 0.15) continue;
        float4 tangentSeed = asfloat(surfacePaintData[record + 4]);
        float3 tangent = tangentSeed.xyz;
        float3 bitangent = cross(direction.xyz, tangent);
        float2 uv = float2(dot(offset, tangent), dot(offset, bitangent));
        float angle = atan2(uv.y, uv.x);
        float phase = tangentSeed.w;
        float edge = center.w * (0.86 + 0.08 * sin(3.0 * angle + phase) + 0.06 * cos(5.0 * angle - phase));
        if (identity.z != 0)
            edge = center.w * (0.88 + 0.08 * sin(dot(position, float3(1.73, 0.91, 1.31))) +
                                      0.04 * sin(dot(position, float3(-0.83, 1.57, 2.11))));
        float r = length(uv) / max(edge, 0.001);
        if (r >= 1.0) continue;
        float4 pigment = asfloat(surfacePaintData[record + 2]);
        float4 material = asfloat(surfacePaintData[record + 3]);
        float footprint = max(length(positionDx), length(positionDy));
        float aa = max(footprint / max(edge, 0.001), 0.025);
        float mask = (1.0 - smoothstep(0.78 - aa, 1.0 + aa, r)) * pigment.w;
        mask *= 1.0 - smoothstep(direction.w * 0.75, direction.w, abs(height));
        float2 q = uv * material.w + phase;
        float detailFade = 1.0 - smoothstep(0.5, 1.5, footprint * material.w);
        float bubbles = sin(q.x) * sin(q.y) + 0.5 * sin(q.x * 0.7 + q.y * 1.3);
        float2 gradient = float2(cos(q.x) * sin(q.y) + 0.35 * cos(q.x * 0.7 + q.y * 1.3),
                                 sin(q.x) * cos(q.y) + 0.65 * cos(q.x * 0.7 + q.y * 1.3));
        float3 bump = (tangent * gradient.x + bitangent * gradient.y) * material.z * detailFade;
        if (identity.z != 0)
        {
            // A common world-space field prevents the surface detail restarting at each segment.
            float4 organic = SurfacePaintNoise(position * material.w + float3(17, 43, 71));
            bubbles = organic.x * 2.0 - 1.0;
            bump = organic.yzw * material.z * detailFade;
            // Union neighboring segments rather than repeatedly coating their overlapping end caps.
            float unionMask = max(ribbonCoverage, mask);
            mask = (unionMask - ribbonCoverage) / max(1.0 - ribbonCoverage, 0.0001);
            ribbonCoverage = unionMask;
        }
        bump -= geometricNormal * dot(bump, geometricNormal);
        float3 paintedColor = pigment.rgb;
        if (identity.y == 0) paintedColor *= 0.94 + 0.04 * bubbles * detailFade;
        color = lerp(color, paintedColor, mask);
        roughness = lerp(roughness, material.x, mask);
        metallic = lerp(metallic, material.y, mask);
        normal = normalize(lerp(normal, normalize(geometricNormal - bump), mask));
        coverage = mask + coverage * (1.0 - mask);
    }
    return coverage;
}
