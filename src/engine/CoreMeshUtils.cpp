#include "Core.h"
namespace Engine {
void Core::GenerateTangents(std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices, uint32_t startIndex, uint32_t indexCount)
{
    std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0f));

    for (uint32_t i = startIndex; i + 2 < startIndex + indexCount; i += 3) {
        uint32_t i0 = indices[i + 0];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        Vertex& v0 = vertices[i0];
        Vertex& v1 = vertices[i1];
        Vertex& v2 = vertices[i2];

        glm::vec3 p0 = v0.position;
        glm::vec3 p1 = v1.position;
        glm::vec3 p2 = v2.position;

        glm::vec2 uv0(v0.uv_x, v0.uv_y);
        glm::vec2 uv1(v1.uv_x, v1.uv_y);
        glm::vec2 uv2(v2.uv_x, v2.uv_y);

        glm::vec3 e1 = p1 - p0;
        glm::vec3 e2 = p2 - p0;

        glm::vec2 duv1 = uv1 - uv0;
        glm::vec2 duv2 = uv2 - uv0;

        float denom = duv1.x * duv2.y - duv2.x * duv1.y;
        if (std::abs(denom) < 1e-8f)
            continue;

        float r = 1.0f / denom;

        glm::vec3 tangent =
            (e1 * duv2.y - e2 * duv1.y) * r;

        glm::vec3 bitangent =
            (e2 * duv1.x - e1 * duv2.x) * r;

        tan1[i0] += tangent;
        tan1[i1] += tangent;
        tan1[i2] += tangent;

        tan2[i0] += bitangent;
        tan2[i1] += bitangent;
        tan2[i2] += bitangent;
    }

    for (uint32_t i = startIndex; i < startIndex + indexCount; i++) {
        uint32_t vi = indices[i];

        glm::vec3 n = glm::normalize(vertices[vi].normal);
        glm::vec3 t = tan1[vi];

        // Gram-Schmidt orthogonalize
        t = glm::normalize(t - n * glm::dot(n, t));

        float handedness =
            glm::dot(glm::cross(n, t), tan2[vi]) < 0.0f ? -1.0f : 1.0f;

        vertices[vi].tangent = glm::vec4(t, handedness);
    }
}
} // end of namespace engine