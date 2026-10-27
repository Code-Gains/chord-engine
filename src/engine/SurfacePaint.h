#pragma once
#include "EntityState.h"
#include "MeshComponent.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace Engine
{
enum class SurfacePaintStyle : uint32_t
{
    Goo,
    Scorch
};

// Runtime material stamp. The caller owns lifetime/opacity; no geometry or damage is implied.
struct SurfacePaint
{
    entt::entity receiver{entt::null};
    glm::vec3 position{}, normal{0, 1, 0}, tangent{1, 0, 0};
    float radius = 1, depth = .5f;
    glm::vec3 color{.13f, .075f, .095f};
    float opacity = 1, roughness = .28f, metallic = 0, bumpStrength = .12f;
    float detailScale = 8, seed = 0;
    SurfacePaintStyle style = SurfacePaintStyle::Goo;
    bool ribbon = false;
    glm::vec3 segmentEnd{};
};

constexpr uint32_t SurfacePaintBuckets = 1024, MaxSurfacePaint = 256;
constexpr float SurfacePaintCellSize = 4.f;
inline uint32_t SurfacePaintReceiverId(entt::entity e)
{
    return uint32_t(entt::to_integral(e)) + 1u;
}
inline uint32_t SurfacePaintHash(glm::ivec3 cell)
{
    return (uint32_t(cell.x) * 73856093u ^ uint32_t(cell.y) * 19349663u ^ uint32_t(cell.z) * 83492791u) &
           (SurfacePaintBuckets - 1);
}
inline bool ValidSurfacePaint(const SurfacePaint &p)
{
    const auto finite = [](glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    return finite(p.position) && glm::all(glm::lessThan(glm::abs(p.position), glm::vec3(1000000))) &&
           finite(p.normal) && glm::length(p.normal) > .01f && finite(p.tangent) && finite(p.color) &&
           std::isfinite(p.radius) && p.radius >= .05f && p.radius <= 8 && std::isfinite(p.depth) && p.depth > 0 &&
           p.depth <= 8 && std::isfinite(p.opacity) && p.opacity > 0 && std::isfinite(p.roughness) &&
           std::isfinite(p.metallic) && std::isfinite(p.bumpStrength) && std::isfinite(p.detailScale) &&
           std::isfinite(p.seed) && (!p.ribbon || (finite(p.segmentEnd) && glm::distance(p.position, p.segmentEnd) <= 8.f)) &&
           (p.style == SurfacePaintStyle::Goo || p.style == SurfacePaintStyle::Scorch);
}

// Header, hash ranges, seven vec4 records per stamp, then bucket references. Float bits are lossless.
inline std::vector<glm::uvec4> PackSurfacePaint(entt::registry &registry)
{
    std::vector<SurfacePaint> stamps;
    for (auto e : registry.view<SurfacePaint>())
    {
        const auto &p = registry.get<SurfacePaint>(e);
        if (!registry.valid(p.receiver) || !registry.all_of<MeshComponent>(p.receiver) ||
            IsEntityDisabled(registry, e) || IsEntityDisabled(registry, p.receiver) || !ValidSurfacePaint(p))
            continue;
        stamps.push_back(p);
        if (stamps.size() == MaxSurfacePaint)
            break;
    }
    if (stamps.empty())
        return {glm::uvec4(0)};
    const uint32_t base = 1 + SurfacePaintBuckets;
    std::vector<glm::uvec4> result(base + stamps.size() * 7);
    result[0] = {uint32_t(stamps.size()), std::bit_cast<uint32_t>(SurfacePaintCellSize), base, 0};
    std::array<std::vector<uint32_t>, SurfacePaintBuckets> buckets;
    const auto bits = [](glm::vec4 v) { return std::bit_cast<glm::uvec4>(v); };
    for (uint32_t i = 0; i < stamps.size(); ++i)
    {
        const auto &p = stamps[i];
        const auto normal = glm::normalize(p.normal);
        auto tangent = p.tangent - normal * glm::dot(normal, p.tangent);
        if (glm::length(tangent) < .01f)
            tangent = glm::cross(normal, std::abs(normal.y) < .9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
        const auto offset = base + i * 7;
        result[offset] = bits(glm::vec4(p.position, p.radius));
        result[offset + 1] = bits(glm::vec4(normal, p.depth));
        result[offset + 2] =
            bits(glm::vec4(glm::clamp(p.color, glm::vec3(0), glm::vec3(1)), std::clamp(p.opacity, 0.f, 1.f)));
        result[offset + 3] =
            bits(glm::vec4(std::clamp(p.roughness, .04f, 1.f), std::clamp(p.metallic, 0.f, 1.f),
                           std::clamp(p.bumpStrength, 0.f, .5f), std::clamp(p.detailScale, .1f, 64.f)));
        result[offset + 4] = bits(glm::vec4(glm::normalize(tangent), p.seed));
        result[offset + 5] = {SurfacePaintReceiverId(p.receiver), uint32_t(p.style), p.ribbon ? 1u : 0u, 0};
        result[offset + 6] = bits(glm::vec4(p.ribbon ? p.segmentEnd : p.position, 0));
        const float extent = p.radius + p.depth;
        const auto end = p.ribbon ? p.segmentEnd : p.position;
        const auto low = glm::ivec3(glm::floor((glm::min(p.position, end) - glm::vec3(extent)) / SurfacePaintCellSize));
        const auto high = glm::ivec3(glm::floor((glm::max(p.position, end) + glm::vec3(extent)) / SurfacePaintCellSize));
        std::vector<uint32_t> touched;
        for (int x = low.x; x <= high.x; ++x)
            for (int y = low.y; y <= high.y; ++y)
                for (int z = low.z; z <= high.z; ++z)
                    touched.push_back(SurfacePaintHash({x, y, z}));
        std::sort(touched.begin(), touched.end());
        touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
        for (auto bucket : touched)
            buckets[bucket].push_back(i);
    }
    for (uint32_t bucket = 0; bucket < SurfacePaintBuckets; ++bucket)
    {
        result[1 + bucket] = {uint32_t(result.size()), uint32_t(buckets[bucket].size()), 0, 0};
        for (auto index : buckets[bucket])
            result.push_back({index, 0, 0, 0});
    }
    return result;
}
} // namespace Engine
