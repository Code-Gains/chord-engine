#include "Core.h"

#include <limits>

namespace Engine {
AllocatedBuffer Core::CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    // allocate buffer
    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;

    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaallocInfo = {};
    vmaallocInfo.usage = memoryUsage;
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    AllocatedBuffer newBuffer;

    // allocate the buffer
    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation,
        &newBuffer.info));

    // Get device address if requested
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = newBuffer.buffer;
        newBuffer.deviceAddress = vkGetBufferDeviceAddress(_device, &addressInfo);
    } else {
        newBuffer.deviceAddress = 0; // not used
    }

    return newBuffer;
}

void Core::DestroyBuffer(const AllocatedBuffer &buffer)
{
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

GPUMeshBuffers Core::UploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers newSurface;

    //create vertex buffer
    newSurface.vertexBuffer = CreateBuffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    //find the adress of the vertex buffer
    VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.vertexBuffer.buffer };
    newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

    //create index buffer
    newSurface.indexBuffer = CreateBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    AllocatedBuffer staging = CreateBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    //void* data = staging.allocation->GetMappedData();
    void* data = staging.info.pMappedData;

    // copy vertex buffer
    memcpy(data, vertices.data(), vertexBufferSize);
    // copy index buffer
    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy vertexCopy{ 0 };
        vertexCopy.dstOffset = 0;
        vertexCopy.srcOffset = 0;
        vertexCopy.size = vertexBufferSize;

        vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

        VkBufferCopy indexCopy{ 0 };
        indexCopy.dstOffset = 0;
        indexCopy.srcOffset = vertexBufferSize;
        indexCopy.size = indexBufferSize;

        vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
    });

    DestroyBuffer(staging);

    return newSurface;
}

std::shared_ptr<MeshAsset> Core::CreateRuntimeMesh(
    std::string name,
    std::span<uint32_t> indices,
    std::span<Vertex> vertices)
{
    if (indices.empty() || vertices.empty()) {
        return {};
    }

    auto mesh = std::make_shared<MeshAsset>();
    mesh->name = std::move(name);
    mesh->runtimeVertexCount = vertices.size();
    mesh->surfaces.push_back(GeoSurface {
        0,
        static_cast<uint32_t>(indices.size()),
        nullptr
    });

    glm::vec3 minPosition(std::numeric_limits<float>::max());
    glm::vec3 maxPosition(std::numeric_limits<float>::lowest());
    for (const auto& vertex : vertices) {
        minPosition = glm::min(minPosition, vertex.position);
        maxPosition = glm::max(maxPosition, vertex.position);
    }

    mesh->boundsCenter = (minPosition + maxPosition) * 0.5f;
    mesh->boundsRadius = glm::max(
        glm::length(maxPosition - mesh->boundsCenter),
        0.01f);

    if (_retainCpuMeshPickingData) {
        mesh->pickingPositions.reserve(vertices.size());
        for (const auto& vertex : vertices) {
            mesh->pickingPositions.push_back(vertex.position);
        }
        mesh->pickingIndices.assign(indices.begin(), indices.end());
    }

    mesh->meshBuffers = UploadMesh(indices, vertices);

    auto vertexBuffer = mesh->meshBuffers.vertexBuffer;
    auto indexBuffer = mesh->meshBuffers.indexBuffer;
    _mainDeletionQueue.push_function([this, vertexBuffer, indexBuffer]() {
        DestroyBuffer(vertexBuffer);
        DestroyBuffer(indexBuffer);
    });

    return mesh;
}
bool Core::UpdateRuntimeMeshVertices(MeshAsset &mesh, std::span<const Vertex> vertices)
{
    const auto bytes = vertices.size_bytes();
    if (vertices.empty() || vertices.size() != mesh.runtimeVertexCount || !mesh.meshBuffers.vertexBuffer.buffer ||
        bytes > mesh.meshBuffers.vertexBuffer.info.size)
        return false;
    auto staging = CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    memcpy(staging.info.pMappedData, vertices.data(), bytes);
    vmaFlushAllocation(_allocator, staging.allocation, 0, bytes);
    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);
        VkBufferCopy copy{0, 0, bytes};
        vkCmdCopyBuffer(cmd, staging.buffer, mesh.meshBuffers.vertexBuffer.buffer, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);
    });
    DestroyBuffer(staging);
    glm::vec3 minimum(std::numeric_limits<float>::max()), maximum(std::numeric_limits<float>::lowest());
    mesh.pickingPositions.clear();
    for (const auto &v : vertices) { minimum = glm::min(minimum, v.position); maximum = glm::max(maximum, v.position); }
    mesh.boundsCenter = (minimum + maximum) * .5f;
    mesh.boundsRadius = std::max(.01f, glm::length(maximum - mesh.boundsCenter));
    if (_retainCpuMeshPickingData)
        for (const auto &v : vertices) mesh.pickingPositions.push_back(v.position);
    return true;
}
} // end of namespace engine
