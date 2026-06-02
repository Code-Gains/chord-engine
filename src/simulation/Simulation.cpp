#include "Simulation.h"
#include "Core.h"
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "Camera.h"
#include <mutex>
#include <future>
#include <thread>



glm::quat Simulation::RandomQuaternion(std::mt19937& rng) const {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    float u1 = dist(rng);
    float u2 = dist(rng);
    float u3 = dist(rng);

    float sqrt1MinusU1 = sqrt(1.0f - u1);
    float sqrtU1 = sqrt(u1);

    float theta1 = 2.0f * glm::pi<float>() * u2;
    float theta2 = 2.0f * glm::pi<float>() * u3;

    return glm::quat(
        sqrt1MinusU1 * sin(theta1),
        sqrt1MinusU1 * cos(theta1),
        sqrtU1 * sin(theta2),
        sqrtU1 * cos(theta2)
    );
}

// Returns a random float in [0, 1)
float Simulation::RandomFloat(std::mt19937& rng, float x, float y) const {
    std::uniform_real_distribution<float> dist(x, y);
    return dist(rng);
}

Simulation::Simulation(entt::registry &registry) : System(registry)
{
}

void Simulation::Initialize(std::shared_ptr<MeshAsset> centerMesh, std::shared_ptr<MeshAsset> particleMesh)
{
    _centerMesh = centerMesh;
    _particleMesh = particleMesh;
    ResetSimulation();
    // std::mt19937 rng(std::random_device{}());
    // // Cube shape
    // int gap = 5;
    // // particle instantiation
    // for (int x = 0; x < 100; x++) {
    //     for (int y = 0; y < 100; y++) {
    //         for (int z = 0; z < 100; z++) {
    //             auto particleEntity = _registry.create();
    //             _registry.emplace<MeshComponent>(particleEntity, particleMesh);
    //             auto transform = Transform();
    //             transform.position = glm::vec3 {x * gap + 100 + RandomFloat(rng, -2.5, 2.5), y * gap + 100 + RandomFloat(rng, -2.5, 2.5),  z * gap + 100 + RandomFloat(rng, -2.5, 2.5)};
    //             transform.rotation = RandomQuaternion(rng);
    //             _registry.emplace<Transform>(particleEntity, transform);
    //             auto physicsBody = PhysicsBody {1.0f, glm::vec3 {0.0f, 0.0f, -1.0f}};
    //             _registry.emplace<PhysicsBody>(particleEntity, physicsBody);
    //         }
    //     }
    // }

    // // center instantiation
    // auto planetEntity = _registry.create();
    // _registry.emplace<MeshComponent>(planetEntity, centerMesh);
    // auto transform = Transform();
    // transform.position = glm::vec3 {-0, -0,  -0};
    // transform.scale = glm::vec3 {0.1f, 0.1f,  0.1f};
    // auto& newTransformReference =  _registry.emplace<Transform>(planetEntity, transform);
    // _attractorTransform = &newTransformReference;
    // _registry.emplace<SingleRenderTag>(planetEntity);

    // auto physicsBody = PhysicsBody {30000.0f, glm::vec3 {0.0f, 0.0f, 0.0f}};
    // auto& newPhysicsBodyReference = _registry.emplace<PhysicsBody>(planetEntity, physicsBody);
    // _attractorPhysicsBody = &newPhysicsBodyReference;

    // auto attractor = Attractor();
    // _registry.emplace<Attractor>(planetEntity, attractor);


    //-----------------------------
    // Chaos Sphere

    // int numParticles = 1000000;
    // float radius = 500.0f;

    // for (int i = 0; i < numParticles; i++) {
    //     auto particleEntity = _registry.create();
    //     _registry.emplace<MeshComponent>(particleEntity, particleMesh);

    //     // Random point inside unit sphere
    //     glm::vec3 dir = glm::sphericalRand(1.0f); // glm helper
    //     float r = cbrt(RandomFloat(rng, 0.5, 1)) * radius; // cubic root for uniform volume
    //     glm::vec3 position = dir * r;
        
    //     Transform transform;
    //     transform.position = position;
    //     transform.rotation = RandomQuaternion(rng);
    //     _registry.emplace<Transform>(particleEntity, transform);

    //     PhysicsBody physicsBody;
    //     physicsBody.mass = 1.0f;
    //     //physicsBody.velocity = glm::normalize(position) * RandomFloat(rng, 0.5f, 2.0f);
    //     physicsBody.velocity = glm::vec3{
    //     RandomFloat(rng, -10000.0f, 10000.0f),
    //     RandomFloat(rng, -10000.0f, 10000.0f),
    //     RandomFloat(rng, -10000.0f, 10000.0f)
    //      };
    //     _registry.emplace<PhysicsBody>(particleEntity, physicsBody);
    // }


    // // center instantiation
    // auto planetEntity = _registry.create();
    // _registry.emplace<MeshComponent>(planetEntity, centerMesh);
    // auto transform = Transform();
    // transform.position = glm::vec3 {-0, -0,  -0};
    // transform.scale = glm::vec3 {0.1f, 0.1f,  0.1f};
    // _registry.emplace<Transform>(planetEntity, transform);
    // _registry.emplace<SingleRenderTag>(planetEntity);

    // auto physicsBody = PhysicsBody {30000.0f, glm::vec3 {0.0f, 0.0f, 0.0f}};
    // _registry.emplace<PhysicsBody>(planetEntity, physicsBody);

    // auto attractor = Attractor();
    // _registry.emplace<Attractor>(planetEntity, attractor);

    // // triangular cube split
    // int gap = 5;
    // // particle instantiation
    // int mainAxis = 0;
    // for (int x = 0; x < 100; x++) {
    //     for (int y = 0; y < 100; y++) {
    //         for (int z = 0; z < 100; z++) {
    //             auto particleEntity = _registry.create();
    //             _registry.emplace<MeshComponent>(particleEntity, particleMesh);
    //             auto transform = Transform();
    //             transform.position = glm::vec3 {x * gap + 100 + RandomFloat(rng, -2.5, 2.5), y * gap + 100 + RandomFloat(rng, -2.5, 2.5),  z * gap + 100 + RandomFloat(rng, -2.5, 2.5)};
    //             transform.rotation = RandomQuaternion(rng);
    //             _registry.emplace<Transform>(particleEntity, transform);
    //             PhysicsBody physicsBody;
    //             if (mainAxis == 0)
    //                 physicsBody = PhysicsBody {1.0f, glm::vec3 {2.0f  - x * 0.015f, 0.0f, 0.0f}};
    //             else if (mainAxis == 1)
    //                 physicsBody = PhysicsBody {1.0f, glm::vec3 {0.0f, 2.0f - y * 0.015f, 0.0f}};
    //             else if (mainAxis == 2)
    //                 physicsBody = PhysicsBody {1.0f, glm::vec3 {0.0f, 0.0f, 2.0f - z * 0.015f}};
    //             _registry.emplace<PhysicsBody>(particleEntity, physicsBody);
    //         }
    //     }
    //     mainAxis++;
    //     if (mainAxis > 2) {
    //         mainAxis = 0;
    //     }
    // }

    // // center instantiation
    // auto planetEntity = _registry.create();
    // _registry.emplace<MeshComponent>(planetEntity, centerMesh);
    // auto transform = Transform();
    // transform.position = glm::vec3 {-0, -0,  -0};
    // transform.scale = glm::vec3 {0.1f, 0.1f,  0.1f};
    // _registry.emplace<Transform>(planetEntity, transform);
    // _registry.emplace<SingleRenderTag>(planetEntity);

    // auto physicsBody = PhysicsBody {100000.0f, glm::vec3 {0.0f, 0.0f, 0.0f}};
    // _registry.emplace<PhysicsBody>(planetEntity, physicsBody);

    // auto attractor = Attractor();
    // _registry.emplace<Attractor>(planetEntity, attractor);
    // ------------------------------------------
    // int gap = 5;
    // // particle instantiation
    // for (int x = 0; x < 100; x++) {
    //     for (int y = 0; y < 100; y++) {
    //         for (int z = 0; z < 100; z++) {
    //             auto particleEntity = _registry.create();
    //             _registry.emplace<MeshComponent>(particleEntity, particleMesh);
    //             auto transform = Transform();
    //             transform.position = glm::vec3 {x * gap + 100 + RandomFloat(rng, -2.5, 2.5), y * gap + 100 + RandomFloat(rng, -2.5, 2.5),  z * gap + 100 + RandomFloat(rng, -2.5, 2.5)};
    //             transform.rotation = RandomQuaternion(rng);
    //             _registry.emplace<Transform>(particleEntity, transform);
    //             auto physicsBody = PhysicsBody {1.0f, glm::vec3 {0.0f, 0.0f, -1.0f}};
    //             _registry.emplace<PhysicsBody>(particleEntity, physicsBody);
    //         }
    //     }
    // }

    // int count = 1000000; // same amount as 100x100x100

    // float innerRadius = 200.0f;
    // float outerRadius = 250.0f;
    // float thickness   = 5.0f;

    // glm::vec3 center = {100.0f, 100.0f, 100.0f};

    // for (int i = 0; i < count; i++) {
    //     auto particleEntity = _registry.create();

    //     _registry.emplace<MeshComponent>(particleEntity, particleMesh);

    //     Transform transform;

    //     // --- ring sampling ---
    //     float angle = RandomFloat(rng, 0.0f, 2.0f * glm::pi<float>());

    //     // uniform distribution across ring area
    //     float r = RandomFloat(rng,
    //        innerRadius,
    //         outerRadius);

    //     float height = RandomFloat(rng, -thickness * 0.5f, thickness * 0.5f);

    //     transform.position = center + glm::vec3{
    //         cos(angle) * r,
    //         height,
    //         sin(angle) * r
    //     };

    //     transform.rotation = RandomQuaternion(rng);

    //     _registry.emplace<Transform>(particleEntity, transform);

    //     // --- velocity (example: outward from center) ---
    //     glm::vec3 radial = glm::normalize(transform.position - center);

    //     // tangent direction (orbit around Y axis)
    //     glm::vec3 tangent = glm::vec3(-radial.z, 0.0f, radial.x);

    //     // optional: randomize direction (clockwise / counterclockwise)
    //     if (RandomFloat(rng, 0.0f, 1.0f) > 0.5f)
    //         tangent = -tangent;

    //     PhysicsBody physicsBody{
    //         1.0f,
    //         tangent * 1.5f//RandomFloat(rng, 5.0f, 5.0f)
    //     };

    //     _registry.emplace<PhysicsBody>(particleEntity, physicsBody);
    // }

    // // center instantiation
    // auto planetEntity = _registry.create();
    // _registry.emplace<MeshComponent>(planetEntity, centerMesh);
    // auto transform = Transform();
    // transform.position = glm::vec3 {0, 0,  0};
    // transform.scale = glm::vec3 {0.1f, 0.1f,  0.1f};
    // _registry.emplace<Transform>(planetEntity, transform);
    // _registry.emplace<SingleRenderTag>(planetEntity);

    // auto physicsBody = PhysicsBody {30000.0f, glm::vec3 {0.0f, 0.0f, 0.0f}};
    // _registry.emplace<PhysicsBody>(planetEntity, physicsBody);

    // auto attractor = Attractor();
    // _registry.emplace<Attractor>(planetEntity, attractor);

}

void Simulation::FixedUpdate(float deltaTime)
{
    deltaTime *= _timeScale;
    // all attractors
    auto attractorRegistryView = _registry.view<Transform, PhysicsBody, Attractor>();
    // all particles
    auto particleRegistryView = _registry.view<Transform, PhysicsBody>(entt::exclude<Attractor>);

    for (auto itA = attractorRegistryView.begin(); itA != attractorRegistryView.end(); ++itA) {
        auto entityA = *itA;

        auto& transformA = attractorRegistryView.get<Transform>(entityA);
        auto& bodyA = attractorRegistryView.get<PhysicsBody>(entityA);

        auto itB = itA;
        ++itB;

        for (; itB != attractorRegistryView.end(); ++itB) {
            auto entityB = *itB;

            auto& transformB = attractorRegistryView.get<Transform>(entityB);
            auto& bodyB = attractorRegistryView.get<PhysicsBody>(entityB);

            glm::vec3 distanceVec = transformB.position - transformA.position;

            float epsilon = 0.001f;
            float distanceSquared = glm::dot(distanceVec, distanceVec) + epsilon;
            glm::vec3 direction = glm::normalize(distanceVec);

            float force = (bodyA.mass * bodyB.mass) / distanceSquared;

            // acceleration = F / m
            glm::vec3 accelA = direction * (force / bodyA.mass);
            glm::vec3 accelB = -direction * (force / bodyB.mass);

            bodyA.velocity += accelA * deltaTime;
            bodyB.velocity += accelB * deltaTime;
        }
    }

    for (auto attractorEntity : attractorRegistryView) {
        auto& attractorTransform = attractorRegistryView.get<Transform>(attractorEntity);
        auto& attractorPhysicsBody = attractorRegistryView.get<PhysicsBody>(attractorEntity);

        for (auto particleEntity : particleRegistryView) {
            auto& particleTransform = particleRegistryView.get<Transform>(particleEntity);
            auto& particlePhysicsBody = particleRegistryView.get<PhysicsBody>(particleEntity);

            glm::vec3 distanceVec = attractorTransform.position - particleTransform.position;
            // F = G * m1 * m2 / R^2 simplified -> M/R^2
            float epsilon = 0.001f;
            float distanceSquared = glm::dot(distanceVec, distanceVec) + epsilon; // add a little to avoid div by 0
            glm::vec3 forceDirection = glm::normalize(distanceVec);

            float forceStrength = attractorPhysicsBody.mass / distanceSquared;

            if (_pulseEnabled) {
                float distance = glm::length(distanceVec) + 0.001f;
                float pulseForce = _pulseStrength / distance;
                auto pulseDirection = -forceDirection;
                particlePhysicsBody.velocity += pulseDirection * pulseForce;
            }

            particlePhysicsBody.velocity += forceDirection * forceStrength * deltaTime;
        }
    }
    _pulseEnabled = false;
    for (auto entity : attractorRegistryView) {
        auto& transform = attractorRegistryView.get<Transform>(entity);
        auto& physicsBody = attractorRegistryView.get<PhysicsBody>(entity);

        transform.position += physicsBody.velocity * deltaTime;
    }
    for (auto entity : particleRegistryView) {
        auto& transform = particleRegistryView.get<Transform>(entity);
        auto& physicsBody = particleRegistryView.get<PhysicsBody>(entity);

        transform.position += physicsBody.velocity * deltaTime;// {0.1f, 0.1f, 0.1f};//physicsBody.velocity * deltaTime;
    }
}

// void Simulation::FixedUpdate(float deltaTime)
// {
//     deltaTime *= _timeScale;

//     auto& pool = Engine::Core::GetThreadPool();

//     // all attractors
//     auto attractorView = _registry.view<Transform, PhysicsBody, Attractor>();
//     // all particles
//     auto particleView = _registry.view<Transform, PhysicsBody>(entt::exclude<Attractor>);

//     size_t numAttractors = std::distance(attractorView.begin(), attractorView.end());
//     size_t numParticles = std::distance(particleView.begin(), particleView.end());

//     // --- Attractor-Attractor interactions ---
//     size_t attractorThreads = pool.threadCount;
//     size_t chunkSizeA = (numAttractors + attractorThreads - 1) / attractorThreads;

//     auto attractorsBegin = attractorView.begin();

//     for (size_t t = 0; t < attractorThreads; ++t) {
//         pool.Enqueue([&, t]() {
//             size_t start = t * chunkSizeA;
//             size_t end = std::min(start + chunkSizeA, numAttractors);

//             auto itA = attractorsBegin;
//             std::advance(itA, start);

//             for (size_t i = start; i < end; ++i, ++itA) {
//                 auto entityA = *itA;
//                 auto& transformA = attractorView.get<Transform>(entityA);
//                 auto& bodyA = attractorView.get<PhysicsBody>(entityA);

//                 auto itB = itA;
//                 ++itB;
//                 for (; itB != attractorView.end(); ++itB) {
//                     auto entityB = *itB;
//                     auto& transformB = attractorView.get<Transform>(entityB);
//                     auto& bodyB = attractorView.get<PhysicsBody>(entityB);

//                     glm::vec3 distVec = transformB.position - transformA.position;
//                     float eps = 0.001f;
//                     float dist2 = glm::dot(distVec, distVec) + eps;
//                     glm::vec3 dir = glm::normalize(distVec);

//                     float force = (bodyA.mass * bodyB.mass) / dist2;
//                     glm::vec3 accelA = dir * (force / bodyA.mass);
//                     glm::vec3 accelB = -dir * (force / bodyB.mass);

//                     bodyA.velocity += accelA * deltaTime;
//                     bodyB.velocity += accelB * deltaTime;
//                 }
//             }
//         });
//     }

//     pool.Wait(); // wait for attractor-attractor threads

//     // --- Attractor-Particle interactions ---
//     size_t particleThreads = pool.threadCount;
//     size_t chunkSizeP = (numParticles + particleThreads - 1) / particleThreads;

//     auto particlesBegin = particleView.begin();

//     for (size_t t = 0; t < particleThreads; ++t) {
//         pool.Enqueue([&, t]() {
//             size_t start = t * chunkSizeP;
//             size_t end = std::min(start + chunkSizeP, numParticles);

//             auto itP = particlesBegin;
//             std::advance(itP, start);

//             for (size_t i = start; i < end; ++i, ++itP) {
//                 auto particleEntity = *itP;
//                 auto& particleTransform = particleView.get<Transform>(particleEntity);
//                 auto& particleBody = particleView.get<PhysicsBody>(particleEntity);

//                 for (auto attractorEntity : attractorView) {
//                     auto& attractorTransform = attractorView.get<Transform>(attractorEntity);
//                     auto& attractorBody = attractorView.get<PhysicsBody>(attractorEntity);

//                     glm::vec3 distVec = attractorTransform.position - particleTransform.position;
//                     float eps = 0.001f;
//                     float dist2 = glm::dot(distVec, distVec) + eps;
//                     glm::vec3 dir = glm::normalize(distVec);
//                     float force = attractorBody.mass / dist2;

//                     if (_pulseEnabled) {
//                         float distance = glm::length(distVec) + 0.001f;
//                         float pulseForce = _pulseStrength / distance;
//                         particleBody.velocity += -dir * pulseForce;
//                     }

//                     particleBody.velocity += dir * force * deltaTime;
//                 }
//             }
//         });
//     }

//     pool.Wait(); // wait for particle threads

//     _pulseEnabled = false;

//     // --- Update positions (can also be parallelized if needed) ---
//     for (auto entity : attractorView) {
//         auto& t = attractorView.get<Transform>(entity);
//         auto& body = attractorView.get<PhysicsBody>(entity);
//         t.position += body.velocity * deltaTime;
//     }

//     for (auto entity : particleView) {
//         auto& t = particleView.get<Transform>(entity);
//         auto& body = particleView.get<PhysicsBody>(entity);
//         t.position += body.velocity * deltaTime;
//     }
// }

void Simulation::DrawUi()
{
    ImGui::Begin("Simulation");
	ImGui::Text("Controls:");
    ImGui::Text("Movement: WASD -> Move");
    ImGui::Text("SPACE -> Up");
    ImGui::Text("SHIFT -> Down");
    ImGui::Text("Shortcuts:");
    ImGui::Text("Maximize: Alt + Enter");
    ImGui::Text("Shut Down: Alt + F4");
    auto cameraView = _registry.view<Camera, Transform>();
    auto entity = *cameraView.begin();
    auto& camera = cameraView.get<Camera>(entity);
    ImGui::Text("Camera Speed");
    ImGui::SliderFloat("##CameraSpeed", &camera.speed, 50.0f, 500.0f);
    // Screenshot request button
    if (ImGui::Button("Take Screenshot")) {
        camera.screenshotRequested = true;
    }
    ImGui::Separator();
    ImGui::Text("Live Simulation Parameters:");
    ImGui::Text("Time Scale");
    ImGui::SliderFloat("##TimeScale", &_timeScale, 1.0f, 100.0f);
    if (!_attractorPhysicsBodies.empty()) {
        ImGui::Text("Attractor Masses");

        for (size_t i = 0; i < _attractorPhysicsBodies.size(); ++i) {
            auto* body = _attractorPhysicsBodies[i];

            if (!body) continue;

            ImGui::PushID(static_cast<int>(i));

            ImGui::SliderFloat(
                "##AttractorMass",
                &body->mass,
                1000.f,
                200000.0f
            );

            ImGui::PopID();
        }
    }
    if (!_attractorTransforms.empty()) {
    ImGui::Text("Attractor Positions");

    for (size_t i = 0; i < _attractorTransforms.size(); ++i) {
        auto* transform = _attractorTransforms[i];

        if (!transform) continue;

        ImGui::PushID(static_cast<int>(i)); // unique scope

        ImGui::SliderFloat3(
            "##AttractorPosition",
            glm::value_ptr(transform->position),
            -10.f,
            10.f
        );

        ImGui::PopID();
        }
    }

    // Camera clear color
    auto registryView = _registry.view<Camera>();
    
    if (!registryView.empty()) {
        auto cameraEntity = *registryView.begin();
        auto& camera = registryView.get<Camera>(cameraEntity);
        ImGui::Text("Clear Color");
        ImGui::ColorEdit4("##ClearColor", glm::value_ptr(camera.clearColor));
    }

    ImGui::Separator();
    ImGui::Text("Pulse Settings");

    // Pulse strength (repulsion magnitude)
    ImGui::Text("Pulse Strength");
    ImGui::SliderFloat("##PulseStrength", &_pulseStrength, 0.0f, 10000.0f);

    if (ImGui::Button("Outward Pulse")) {
        _pulseEnabled = true;
    }

    ImGui::Separator();

    ImGui::Text("Simulation Type");
    const char* simTypes[] = { "Cube", "Trilateral Cube", "Rings", "Displaced Rings", "Dual", "Solar System", "Sphere" }; // add more later
    int currentTypeIndex = static_cast<int>(_currentSimulationType);
    if (ImGui::Combo("##SimulationType", &currentTypeIndex, simTypes, IM_ARRAYSIZE(simTypes))) {
        _currentSimulationType = static_cast<SimulationType>(currentTypeIndex);
    }
    
    // Reset button
    if (ImGui::Button("Reset Simulation")) {
        ResetSimulation();  // call your reset function
    }

	ImGui::End();
}

void Simulation::Draw()
{
}

void Simulation::CreateBalancedParticleOnDisk(std::mt19937 &rng, const glm::vec3 &position, float attractorMass)
{
    auto particleEntity = _registry.create();
    _registry.emplace<MeshComponent>(particleEntity, _particleMesh);
    auto transform = Transform();
    transform.position = position;
    transform.rotation = RandomQuaternion(rng);
    _registry.emplace<Transform>(particleEntity, transform);

    glm::vec3 radiusVector = transform.position - glm::vec3{0.0f,0.0f,0.0f};
    float r = glm::length(radiusVector);
    float v = std::sqrt(attractorMass / r);

    glm::vec3 up = glm::abs(radiusVector.y) < 0.99f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    glm::vec3 tangentialDir = glm::normalize(glm::cross(radiusVector, up));
    auto physicsBody = PhysicsBody {1.0f, tangentialDir * v};
    _registry.emplace<PhysicsBody>(particleEntity, physicsBody);
}

void Simulation::CreateOrbitalDisk(std::mt19937 &rng, int count, float innerRadius, float outerRadius, float thickness, float attractorMass, float verticalOffset)
{
    std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    for (int i = 0; i < count; i++) {
        float angle = angleDist(rng);

        // sample radius PER particle
        float t = dist01(rng);
        float radius = sqrt(innerRadius*innerRadius + t * (outerRadius*outerRadius - innerRadius*innerRadius));

        glm::vec3 pos;
        pos.x = cos(angle) * radius;
        pos.z = sin(angle) * radius;

        pos.y = verticalOffset;

        CreateBalancedParticleOnDisk(rng, pos, attractorMass);
    }
}

void Simulation::CreateBalancedParticleOnSphere(std::mt19937 &rng, const glm::vec3 &position, float attractorMass)
{
    auto particleEntity = _registry.create();
    _registry.emplace<MeshComponent>(particleEntity, _particleMesh);

    // Transform
    auto transform = Transform();
    transform.position = position;
    transform.rotation = RandomQuaternion(rng);
    _registry.emplace<Transform>(particleEntity, transform);

    // Radial vector from center
    glm::vec3 radiusVector = glm::normalize(transform.position); // normalized direction from origin
    float r = glm::length(transform.position);

    // Orbital speed for circular motion
    float speed = std::sqrt(attractorMass / r);

    // Pick a random tangent direction
    glm::vec3 randomVec;
    // Generate a vector not parallel to radiusVector
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    do {
        randomVec = glm::vec3(dist(rng), dist(rng), dist(rng));
    } while (glm::length(glm::cross(radiusVector, randomVec)) < 1e-3f);

    glm::vec3 tangentialDir = glm::normalize(glm::cross(radiusVector, randomVec));

    auto physicsBody = PhysicsBody{1.0f, tangentialDir * speed};
    _registry.emplace<PhysicsBody>(particleEntity, physicsBody);
}

void Simulation::CreateOrbitalSphere(std::mt19937 &rng, int count, float radius, float attractorMass)
{
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());

    for (int i = 0; i < count; i++) {
        float u = dist01(rng); // for z-coordinate
        float theta = angleDist(rng); // azimuthal angle

        // sample z uniformly in [-radius, radius]
        float z = (2.0f * u - 1.0f) * radius;

        // radius of circle at z
        float r = sqrt(radius * radius - z * z);

        glm::vec3 pos;
        pos.x = r * cos(theta);
        pos.y = r * sin(theta);
        pos.z = z;

        CreateBalancedParticleOnSphere(rng, pos, attractorMass); // reuse your particle creation function
    }
}

void Simulation::ResetSimulation()
{
    // Clear all particles
    auto particleRegistryView = _registry.view<Transform, PhysicsBody>(entt::exclude<Attractor>);
    for (auto entity : particleRegistryView) {
        _registry.destroy(entity);
    }

    // Clear all attractors
    auto attractorRegistryView = _registry.view<Transform, PhysicsBody, Attractor>();
    for (auto entity : attractorRegistryView) {
        _registry.destroy(entity);
    }

    // Reset cached pointers
    _attractorPhysicsBodies.clear();
    _attractorTransforms.clear();


    switch (_currentSimulationType) {
    case SimulationType::Cube:
        InitializeCubeSimulation();
        break;
    case SimulationType::TrilateralCube:
        InitializeTrilateralCubeSimulation();
        break;
    case SimulationType::Rings:
        InitializeRingsSimulation();
        break;
    case SimulationType::DisplacedRings:
        InitializeDisplacedRingsSimulation();
        break;
    case SimulationType::Dual:
        InitializeDualSimulation();
        break;
    case SimulationType::SolarSystem:
        InitializeSolarSystemSimulation();
        break;
    case SimulationType::Sphere:
        InitializeSphereSimulation();
        break;
}

}

void Simulation::InitializeCubeSimulation()
{
    std::mt19937 rng(std::random_device{}());
    // Cube shape
    int gap = 5;
    float particleSpeed = 5.0f;
    // particle instantiation
    for (int x = 0; x < 100; x++) {
        for (int y = 0; y < 100; y++) {
            for (int z = 0; z < 100; z++) {
                auto particleEntity = _registry.create();
                _registry.emplace<MeshComponent>(particleEntity, _particleMesh);
                auto transform = Transform();
                transform.position = glm::vec3 {x * gap + 100 + RandomFloat(rng, -2.5, 2.5), y * gap + 100 + RandomFloat(rng, -2.5, 2.5),  z * gap + 100 + RandomFloat(rng, -2.5, 2.5)};
                transform.rotation = RandomQuaternion(rng);
                _registry.emplace<Transform>(particleEntity, transform);
                auto physicsBody = PhysicsBody {1.0f, glm::vec3 {0.0f, 0.0f, -particleSpeed}};
                _registry.emplace<PhysicsBody>(particleEntity, physicsBody);
            }
        }
    }

    // center instantiation
    auto planetEntity = _registry.create();
    _registry.emplace<MeshComponent>(planetEntity, _centerMesh);
    auto transform = Transform();
    transform.position = glm::vec3 {-0, -0,  -0};
    transform.scale = glm::vec3 {0.1f, 0.1f,  0.1f};
    auto& newTransformReference =  _registry.emplace<Transform>(planetEntity, transform);
    _attractorTransforms.push_back(&newTransformReference);
    _registry.emplace<SingleRenderTag>(planetEntity);

    auto physicsBody = PhysicsBody {30000.0f, glm::vec3 {0.0f, 0.0f, 0.0f}};
    auto& newPhysicsBodyReference = _registry.emplace<PhysicsBody>(planetEntity, physicsBody);
    _attractorPhysicsBodies.push_back(&newPhysicsBodyReference);

    auto attractor = Attractor();
    _registry.emplace<Attractor>(planetEntity, attractor);
}

void Simulation::InitializeTrilateralCubeSimulation()
{
    std::mt19937 rng(std::random_device{}());
    // triangular cube split
    int gap = 5;
    float particleSpeed = 5.0f;
    // particle instantiation
    int mainAxis = 0;
    for (int x = 50; x < 100; x++) {
        for (int y = 50; y < 100; y++) {
            for (int z = 50; z < 100; z++) {
                auto particleEntity = _registry.create();
                _registry.emplace<MeshComponent>(particleEntity, _particleMesh);
                auto transform = Transform();
                transform.position = glm::vec3 {x * gap + 100 + RandomFloat(rng, -2.5, 2.5), y * gap + 100 + RandomFloat(rng, -2.5, 2.5),  z * gap + 100 + RandomFloat(rng, -2.5, 2.5)};
                transform.rotation = RandomQuaternion(rng);
                _registry.emplace<Transform>(particleEntity, transform);
                PhysicsBody physicsBody;
                if (mainAxis == 0)
                    physicsBody = PhysicsBody {1.0f, glm::vec3 {particleSpeed  - x * 0.015f, 0.0f, 0.0f}};
                else if (mainAxis == 1)
                    physicsBody = PhysicsBody {1.0f, glm::vec3 {0.0f, particleSpeed - y * 0.015f, 0.0f}};
                else if (mainAxis == 2)
                    physicsBody = PhysicsBody {1.0f, glm::vec3 {0.0f, 0.0f, particleSpeed - z * 0.015f}};
                _registry.emplace<PhysicsBody>(particleEntity, physicsBody);
            }
        }
        mainAxis++;
        if (mainAxis > 2) {
            mainAxis = 0;
        }
    }

    // center instantiation
    auto planetEntity = _registry.create();
    _registry.emplace<MeshComponent>(planetEntity, _centerMesh);
    auto transform = Transform();
    transform.position = glm::vec3 {-0, -0,  -0};
    transform.scale = glm::vec3 {0.1f, 0.1f,  0.1f};
    auto& newTransformReference = _registry.emplace<Transform>(planetEntity, transform);
    _attractorTransforms.push_back(&newTransformReference);
    _registry.emplace<SingleRenderTag>(planetEntity);

    auto physicsBody = PhysicsBody {30000.0f, glm::vec3 {0.0f, 0.0f, 0.0f}};
    auto& newPhysicsBodyReference = _registry.emplace<PhysicsBody>(planetEntity, physicsBody);
    _attractorPhysicsBodies.push_back(&newPhysicsBodyReference);

    auto attractor = Attractor();
    _registry.emplace<Attractor>(planetEntity, attractor);
}

void Simulation::InitializeDisplacedRingsSimulation()
{
    std::mt19937 rng(std::random_device{}());
    float attractorMass = 1000.0f;
    // CreateOrbitalDisk(rng, 10000, 100.0f, 150.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 200.0f, 250.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 300.0f, 350.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 400.0f, 450.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 500.0f, 550.0f, 10.0f, attractorMass);

    CreateOrbitalDisk(rng, 10000, 50.0f, 70.0f, 10.0f, attractorMass, 20.0f);
    CreateOrbitalDisk(rng, 15000, 80.0f, 100.0f, 10.0f, attractorMass, 20.0f);
    CreateOrbitalDisk(rng, 15000, 110.0f, 115.0f, 10.0f, attractorMass, 20.0f);
    CreateOrbitalDisk(rng, 15000, 120.0f, 135.0f, 10.0f, attractorMass, 20.0f);
    CreateOrbitalDisk(rng, 15000, 140.0f, 150.0f, 10.0f, attractorMass, 20.0f);
    //CreateBalancedParticle(rng, glm::vec3 { 20.0f, 20.0f, 20.0f }, attractorMass);
    // auto particleEntity = _registry.create();
    // _registry.emplace<MeshComponent>(particleEntity, _particleMesh);
    // auto t = Transform();
    // t.position = glm::vec3 { 20.0f, 20.0f, 20.0f };
    // t.rotation = RandomQuaternion(rng);
    // _registry.emplace<Transform>(particleEntity, t);

    // glm::vec3 radiusVector = t.position - glm::vec3{0.0f,0.0f,0.0f};
    // float r = glm::length(radiusVector);
    // float v = std::sqrt(1000.0f / r);

    // glm::vec3 up = glm::abs(radiusVector.y) < 0.99f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    // glm::vec3 tangentialDir = glm::normalize(glm::cross(radiusVector, up));
    // auto pb = PhysicsBody {1.0f, tangentialDir * v};
    // _registry.emplace<PhysicsBody>(particleEntity, pb);

     // center instantiation
    auto planetEntity = _registry.create();
    _registry.emplace<MeshComponent>(planetEntity, _centerMesh);
    auto transform = Transform();
    transform.position = glm::vec3 {0.0f, 0.0f, 0.0f};
    transform.scale = glm::vec3 {1.0f, 1.0f,  1.0f};
    auto& newTransformReference =  _registry.emplace<Transform>(planetEntity, transform);
    _attractorTransforms.push_back(&newTransformReference);
    _registry.emplace<SingleRenderTag>(planetEntity);

    auto physicsBody = PhysicsBody {attractorMass, glm::vec3 {0.0f, 0.0f, 0.0f}};
    auto& newPhysicsBodyReference = _registry.emplace<PhysicsBody>(planetEntity, physicsBody);
    _attractorPhysicsBodies.push_back(&newPhysicsBodyReference);

    auto attractor = Attractor();
    _registry.emplace<Attractor>(planetEntity, attractor);
}

void Simulation::InitializeRingsSimulation()
{
    std::mt19937 rng(std::random_device{}());
    float attractorMass = 1000.0f;
    // CreateOrbitalDisk(rng, 10000, 100.0f, 150.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 200.0f, 250.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 300.0f, 350.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 400.0f, 450.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 500.0f, 550.0f, 10.0f, attractorMass);

    CreateOrbitalDisk(rng, 3000, 50.0f, 70.0f, 10.0f, attractorMass, 0.0f);
    CreateOrbitalDisk(rng, 5000, 80.0f, 100.0f, 10.0f, attractorMass, 0.0f);
    CreateOrbitalDisk(rng, 5000, 110.0f, 130.0f, 10.0f, attractorMass, 0.0f);
    CreateOrbitalDisk(rng, 5000, 140.0f, 160.0f, 10.0f, attractorMass, 0.0f);
    CreateOrbitalDisk(rng, 5000, 170.0f, 190.0f, 10.0f, attractorMass, 0.0f);
    //CreateBalancedParticle(rng, glm::vec3 { 20.0f, 20.0f, 20.0f }, attractorMass);
    // auto particleEntity = _registry.create();
    // _registry.emplace<MeshComponent>(particleEntity, _particleMesh);
    // auto t = Transform();
    // t.position = glm::vec3 { 20.0f, 20.0f, 20.0f };
    // t.rotation = RandomQuaternion(rng);
    // _registry.emplace<Transform>(particleEntity, t);

    // glm::vec3 radiusVector = t.position - glm::vec3{0.0f,0.0f,0.0f};
    // float r = glm::length(radiusVector);
    // float v = std::sqrt(1000.0f / r);

    // glm::vec3 up = glm::abs(radiusVector.y) < 0.99f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    // glm::vec3 tangentialDir = glm::normalize(glm::cross(radiusVector, up));
    // auto pb = PhysicsBody {1.0f, tangentialDir * v};
    // _registry.emplace<PhysicsBody>(particleEntity, pb);

     // center instantiation
    auto planetEntity = _registry.create();
    _registry.emplace<MeshComponent>(planetEntity, _centerMesh);
    auto transform = Transform();
    transform.position = glm::vec3 {0.0f, 0.0f, 0.0f};
    transform.scale = glm::vec3 {1.0f, 1.0f,  1.0f};
    auto& newTransformReference =  _registry.emplace<Transform>(planetEntity, transform);
    _attractorTransforms.push_back(&newTransformReference);
    _registry.emplace<SingleRenderTag>(planetEntity);

    auto physicsBody = PhysicsBody {attractorMass, glm::vec3 {0.0f, 0.0f, 0.0f}};
    auto& newPhysicsBodyReference = _registry.emplace<PhysicsBody>(planetEntity, physicsBody);
    _attractorPhysicsBodies.push_back(&newPhysicsBodyReference);

    auto attractor = Attractor();
    _registry.emplace<Attractor>(planetEntity, attractor);
}

void Simulation::InitializeDualSimulation()
{
    std::mt19937 rng(std::random_device{}());
    float attractorMass = 1000.0f;
    // CreateOrbitalDisk(rng, 10000, 100.0f, 150.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 200.0f, 250.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 300.0f, 350.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 400.0f, 450.0f, 10.0f, attractorMass);
    // CreateOrbitalDisk(rng, 10000, 500.0f, 550.0f, 10.0f, attractorMass);

    CreateOrbitalDisk(rng, 5000, 50.0f, 70.0f, 10.0f, attractorMass * 2, 0.0f);
    CreateOrbitalDisk(rng, 10000, 80.0f, 100.0f, 10.0f, attractorMass * 2, 0.0f);
    CreateOrbitalDisk(rng, 10000, 110.0f, 130.0f, 10.0f, attractorMass * 2, 0.0f);
    CreateOrbitalDisk(rng, 10000, 140.0f, 160.0f, 10.0f, attractorMass * 2, 0.0f);
    CreateOrbitalDisk(rng, 10000, 170.0f, 190.0f, 10.0f, attractorMass * 2, 0.0f);

    float distance = 20.0f; // separation between stars

    // orbital velocity (for equal masses)
    float v = std::sqrt(attractorMass / (2.0f * distance));

    glm::vec3 posA = glm::vec3{-distance * 0.5f, 0.0f, 0.0f};
    glm::vec3 posB = glm::vec3{ distance * 0.5f, 0.0f, 0.0f};

    // perpendicular direction (orbit plane)
    glm::vec3 velDir = glm::vec3{0.0f, 0.0f, 1.0f};

    //
    // Star A
    //
    auto starA = _registry.create();
    _registry.emplace<MeshComponent>(starA, _centerMesh);

    auto& tA = _registry.emplace<Transform>(starA, Transform{
        posA,
        glm::quat{},
        glm::vec3{5.0f}
    });
    _attractorTransforms.push_back(&tA);

    auto& pbA = _registry.emplace<PhysicsBody>(starA, PhysicsBody{
        attractorMass,
        velDir * v
    });
    _attractorPhysicsBodies.push_back(&pbA);

    _registry.emplace<Attractor>(starA);
    _registry.emplace<SingleRenderTag>(starA);

    //
    // Star B
    //
    auto starB = _registry.create();
    _registry.emplace<MeshComponent>(starB, _centerMesh);

    auto& tB = _registry.emplace<Transform>(starB, Transform{
        posB,
        glm::quat{},
        glm::vec3{5.0f}
    });
    _attractorTransforms.push_back(&tB);

    auto& pbB = _registry.emplace<PhysicsBody>(starB, PhysicsBody{
        attractorMass,
        -velDir * v
    });
    _attractorPhysicsBodies.push_back(&pbB);

    _registry.emplace<Attractor>(starB);
    _registry.emplace<SingleRenderTag>(starB);
}

void Simulation::InitializeSolarSystemSimulation()
{
    std::mt19937 rng(std::random_device{}());

    float starMass = 1000.0f; 
    //CreateOrbitalSphere(rng, 100000, 250.0f, starMass);
    CreateOrbitalDisk(rng, 5000, 100.0f, 150.0f, 10.0f, starMass, 0.0f);
    CreateOrbitalDisk(rng, 10000, 200.0f, 250.0f, 10.0f, starMass, 0.0f);
    CreateOrbitalDisk(rng, 20000, 300.0f, 350.0f, 10.0f, starMass, 0.0f);
    glm::vec3 starScale{50.0f}; // 10x bigger

    // --- Central Star ---
    auto star = _registry.create();
    _registry.emplace<MeshComponent>(star, _centerMesh);

    auto& tStar = _registry.emplace<Transform>(star, Transform{
        glm::vec3{0.0f},
        glm::quat{},
        starScale
    });
    _attractorTransforms.push_back(&tStar);

    auto& pbStar = _registry.emplace<PhysicsBody>(star, PhysicsBody{
        starMass,
        glm::vec3{0.0f}
    });
    _attractorPhysicsBodies.push_back(&pbStar);

    _registry.emplace<Attractor>(star);
    _registry.emplace<SingleRenderTag>(star);

    // --- Planets ---
    struct PlanetInfo {
        float distance;
        float mass;
        float size;
    };

    std::vector<PlanetInfo> planets = {
        {200.0f, 1.0f, 5.0f},   // Mercury-ish
        {300.0f, 2.0f, 6.0f},   // Venus-ish
        {400.0f, 2.0f, 6.5f},   // Earth-ish
        {550.0f, 1.5f, 5.5f},   // Mars-ish
        {700.0f, 5.0f, 10.0f}   // Jupiter-ish
    };

    for (auto& p : planets)
    {
        auto planet = _registry.create();
        _registry.emplace<MeshComponent>(planet, _centerMesh);

        glm::vec3 pos = glm::vec3{p.distance, 0.0f, 0.0f};
        auto& t = _registry.emplace<Transform>(planet, Transform{
            pos,
            glm::quat{},
            glm::vec3{p.size}
        });
        _attractorTransforms.push_back(&t);

        // Circular orbit velocity adjusted for scale: v = sqrt(G*M / r)
        glm::vec3 velDir = glm::vec3{0.0f, 0.0f, 1.0f};
        float v = std::sqrt(starMass / p.distance); 
        auto& pb = _registry.emplace<PhysicsBody>(planet, PhysicsBody{
            p.mass,
            velDir * v
        });
        _attractorPhysicsBodies.push_back(&pb);

        _registry.emplace<Attractor>(planet);
        _registry.emplace<SingleRenderTag>(planet);
    }
}

void Simulation::InitializeSphereSimulation()
{
        std::mt19937 rng(std::random_device{}());

    float starMass = 1000.0f; 
    CreateOrbitalSphere(rng, 100000, 50.0f, starMass);
    glm::vec3 starScale{0.0f};
    // --- Central Star ---
    auto star = _registry.create();
    _registry.emplace<MeshComponent>(star, _centerMesh);

    auto& tStar = _registry.emplace<Transform>(star, Transform{
        glm::vec3{0.0f},
        glm::quat{},
        starScale
    });
    _attractorTransforms.push_back(&tStar);

    auto& pbStar = _registry.emplace<PhysicsBody>(star, PhysicsBody{
        starMass,
        glm::vec3{0.0f}
    });
    _attractorPhysicsBodies.push_back(&pbStar);

    _registry.emplace<Attractor>(star);
    _registry.emplace<SingleRenderTag>(star);
}

int main () {
    Engine::Core core;
    core.Init();

    std::vector<std::shared_ptr<MeshAsset>> helmetMeshes;
    helmetMeshes = core.LoadGltfMeshes(&core, "../../../assets/DamagedHelmet.gltf").value();

    std::vector<std::shared_ptr<MeshAsset>> cubeMeshes;
    cubeMeshes = core.LoadGltfMeshes(&core, "../../../assets/BoxTextured.gltf").value();

    auto& registry = core.GetRegistry();
    auto simulation = std::make_unique<Simulation>(registry);
    simulation->Initialize(helmetMeshes[0], cubeMeshes[0]); // planet mesh and particle mesh
    core._systems.push_back(std::move(simulation));
    
    core.Run();
    return 0;
}
