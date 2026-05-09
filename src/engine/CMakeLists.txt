# vk-bootstrap library
add_library(vkbootstrap STATIC
    ${vkbootstrap_SOURCE_DIR}/src/VkBootstrap.cpp
)

target_include_directories(vkbootstrap
    PUBLIC
        ${vkbootstrap_SOURCE_DIR}/src
        ${VulkanSdkDir}/include
)


# Engine library
add_library(engine STATIC
    Core.cpp
    Renderer.cpp
    VulkanDevice.cpp
    WindowGLFW.cpp
    vk_initializers.cpp
    vk_images.cpp
    vk_pipelines.cpp
    vk_descriptors.cpp
    EcsDebugger.cpp
    Camera.cpp
    Transform.cpp
    InputSystem.cpp
    System.cpp
    RenderingSystem.cpp
    CameraSystem.cpp
    PhysicsBody.cpp
    NameComponent.cpp
)


target_compile_definitions(engine PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_compile_options(engine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/wd5105>       # disable MSVC specific warning if needed
    $<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated> # Clang warning suppression
)

# Include engine headers + dependencies
target_include_directories(engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}     # engine headers
        ${glfw_SOURCE_DIR}/include      # GLFW headers
        ${glm_SOURCE_DIR}               # GLM headers
)

# Vulkan library (cross-platform)
if(WIN32)
    set(VulkanLib "${VulkanSdkDir}/Lib/vulkan-1.lib")
elseif(UNIX)
    set(VulkanLib "${VulkanSdkDir}/lib/libvulkan.so")
endif()

# Link libraries
target_link_libraries(engine
    PUBLIC
        Vulkan::Vulkan
        vkbootstrap
        glfw
        imgui
        fastgltf
         EnTT::EnTT
)

# Get all compute shaders in SHADER_DIR
file(GLOB ALL_SHADERS
    "${SHADER_DIR}/*.comp"
    "${SHADER_DIR}/*.vert"
    "${SHADER_DIR}/*.frag"
)

# List to store output SPIR-V files
set(COMPILED_SHADERS "")

foreach(SHADER_FILE IN LISTS ALL_SHADERS)
    # Get the file name only (e.g., gradient.comp)
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)

    # Output SPIR-V path (same folder, same name, .spv)
    set(SPIRV_FILE "${SHADER_DIR}/${SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT "${SPIRV_FILE}"
        COMMAND glslc -c "${SHADER_FILE}" -o "${SPIRV_FILE}"
        DEPENDS "${SHADER_FILE}"
        COMMENT "Compiling shader ${SHADER_NAME}"
        VERBATIM
    )

    # Add this SPIR-V file to the list of compiled shaders
    list(APPEND COMPILED_SHADERS "${SPIRV_FILE}")
endforeach()

# Custom target that builds all shaders
add_custom_target(engine_shaders ALL DEPENDS ${COMPILED_SHADERS})

# Make engine depend on the shaders
add_dependencies(engine engine_shaders)