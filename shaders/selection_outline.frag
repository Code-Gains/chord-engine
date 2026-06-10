#version 460

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D selectionMask;

layout(push_constant) uniform constants
{
    vec4 color;
    vec2 texelSize;
    float thickness;
    float pad0;
} PushConstants;

void main()
{
    float center = texture(selectionMask, inUV).r;
    if (center > 0.5) {
        discard;
    }

    bool nearSelection = false;
    int radius = int(ceil(PushConstants.thickness));

    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            vec2 offset = vec2(float(x), float(y));
            if (length(offset) > PushConstants.thickness + 0.5) {
                continue;
            }

            float sampleMask =
                texture(selectionMask, inUV + offset * PushConstants.texelSize).r;
            if (sampleMask > 0.5) {
                nearSelection = true;
            }
        }
    }

    if (!nearSelection) {
        discard;
    }

    outColor = PushConstants.color;
}
