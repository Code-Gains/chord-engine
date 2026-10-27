#include "SurfacePaint.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void Check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
int main()
{
    try
    {
        entt::registry r;
        Check(Engine::PackSurfacePaint(r).size() == 1, "Empty paint uses a minimal disabled buffer");
        auto receiver = r.create();
        r.emplace<MeshComponent>(receiver);
        auto other = r.create();
        r.emplace<MeshComponent>(other);
        auto mark = r.create();
        auto &p = r.emplace<Engine::SurfacePaint>(mark);
        p.receiver = receiver;
        p.position = {-4.1f, 2, 7.9f};
        auto packed = Engine::PackSurfacePaint(r);
        Check(packed[0].x == 1, "A valid stamp uploads");
        const auto record = packed[0].z;
        Check(packed[record + 5].x == Engine::SurfacePaintReceiverId(receiver) &&
                  packed[record + 5].x != Engine::SurfacePaintReceiverId(other),
              "Receiver identity is explicit");
        Check(std::bit_cast<float>(packed[record].x) == p.position.x, "Position float bits are preserved");
        for (auto offset : {glm::vec3(0), glm::vec3(.8f, 0, 0), glm::vec3(0, 0, -.8f)})
        {
            const auto cell = glm::ivec3(glm::floor((p.position + offset) / Engine::SurfacePaintCellSize));
            const auto range = packed[1 + Engine::SurfacePaintHash(cell)];
            Check(range.y > 0, "All covered cells reference the stamp, including negative coordinates");
            unsigned matches = 0;
            for (unsigned i = 0; i < range.y; ++i)
                matches += packed[range.x + i].x == 0;
            Check(matches == 1, "Hash collisions never duplicate a stamp in one bucket");
        }
        r.emplace<DisabledEntityTag>(receiver);
        Check(Engine::PackSurfacePaint(r)[0].x == 0, "Disabled receivers are excluded");
        r.remove<DisabledEntityTag>(receiver);
        p.ribbon = true;
        p.segmentEnd = p.position + glm::vec3(7, 0, 0);
        packed = Engine::PackSurfacePaint(r);
        const auto endCell = glm::ivec3(glm::floor(p.segmentEnd / Engine::SurfacePaintCellSize));
        Check(packed[1 + Engine::SurfacePaintHash(endCell)].y > 0, "Ribbon endpoint cells are indexed");
        Check(packed[packed[0].z + 5].z == 1, "Ribbon shape is uploaded");
        p.segmentEnd.x = std::numeric_limits<float>::infinity();
        Check(Engine::PackSurfacePaint(r)[0].x == 0, "Invalid ribbon endpoints are rejected");
        p.ribbon = false;
        p.radius = std::numeric_limits<float>::quiet_NaN();
        Check(Engine::PackSurfacePaint(r)[0].x == 0, "Invalid stamps are rejected");
        p.radius = 1;
        for (unsigned i = 0; i < 300; ++i)
            r.emplace<Engine::SurfacePaint>(r.create(), p);
        packed = Engine::PackSurfacePaint(r);
        Check(packed[0].x == Engine::MaxSurfacePaint, "Upload count is bounded");
        r.destroy(receiver);
        Check(Engine::PackSurfacePaint(r)[0].x == 0, "Destroyed receivers cannot retain paint");
        std::cout << "Surface paint packing, bounds, hash coverage and receiver tests passed.\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
