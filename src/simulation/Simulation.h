#pragma once
#include "System.h"
#include "MeshComponent.h"
#include <random>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include "PhysicsBody.h"
#include "Transform.h"

enum class SimulationType {
    Cube,
    TrilateralCube,
    Rings,
    DisplacedRings,
    Dual,
    SolarSystem,
    Sphere
};

class Simulation : public System {
    glm::quat RandomQuaternion(std::mt19937& rng) const;
    float RandomFloat(std::mt19937& rng, float x, float y) const;
    // exposed parameters
    std::vector<PhysicsBody*> _attractorPhysicsBodies;
    std::vector<Transform*> _attractorTransforms;
    std::shared_ptr<MeshAsset> _centerMesh;
    std::shared_ptr<MeshAsset> _particleMesh;
    SimulationType _currentSimulationType = SimulationType::Rings;
    float _timeScale = 1.0f;
    bool _pulseEnabled = false;
    float _pulseStrength = 500.0f;

public:
    Simulation(entt::registry& registry);
    void Initialize(std::shared_ptr<MeshAsset> centerMesh, std::shared_ptr<MeshAsset> particleMesh);
    void Update(float deltaTime) override {};
    void FixedUpdate(float deltaTime) override;
    virtual void DrawUi() override;
    virtual void Draw() override;

    void CreateBalancedParticleOnDisk(std::mt19937& rng, const glm::vec3& position, float attractorMass);
    void CreateOrbitalDisk(std::mt19937& rng, int count, float innerRadius, float outerRadius, float thickness, float attractorMass, float verticalOffset);
    void CreateBalancedParticleOnSphere(std::mt19937& rng, const glm::vec3& position, float attractorMass);
    void CreateOrbitalSphere(std::mt19937 &rng, int count, float radius, float attractorMass);


    //void StartSimulation();
    void ResetSimulation();
    void InitializeCubeSimulation();
    void InitializeTrilateralCubeSimulation();
    void InitializeDisplacedRingsSimulation();
    void InitializeRingsSimulation();
    void InitializeDualSimulation();
    void InitializeSolarSystemSimulation();
    void InitializeSphereSimulation();
};