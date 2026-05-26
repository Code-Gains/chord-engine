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
    CameraSystem.cpp
    PhysicsBody.cpp
    NameComponent.cpp
    SunlightComponent.cpp
    CoreGltfLoader.cpp
    CoreInitializer.cpp
    CoreSwapchain.cpp
    CorePipelines.cpp
    CoreDescriptors.cpp
    CoreBuffers.cpp
    CoreImages.cpp
    CoreIBL.cpp
    CoreMeshUtils.cpp
)

target_compile_definitions(engine PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_compile_options(engine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/wd5105>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-deprecated>
)

target_include_directories(engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${glfw_SOURCE_DIR}/include
        ${glm_SOURCE_DIR}
)

# ---------------------
# glslc shader compiler
# ---------------------
if(WIN32)
    set(GLSLC_EXECUTABLE "$ENV{VULKAN_SDK}/Bin/glslc.exe")
elseif(UNIX)
    set(GLSLC_EXECUTABLE "$ENV{VULKAN_SDK}/bin/glslc")
endif()

if(NOT EXISTS "${GLSLC_EXECUTABLE}")
    find_program(GLSLC_EXECUTABLE glslc)
endif()

if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Install Vulkan SDK or make sure glslc is in PATH.")
endif()

message(STATUS "Using glslc: ${GLSLC_EXECUTABLE}")

target_link_libraries(engine
    PUBLIC
        Vulkan::Vulkan
        vk-bootstrap
        glfw
        imgui
        fastgltf
        EnTT::EnTT
)

# ---------------------
# Shaders
# ---------------------
file(GLOB ALL_SHADERS CONFIGURE_DEPENDS
    "${SHADER_DIR}/*.comp"
    "${SHADER_DIR}/*.vert"
    "${SHADER_DIR}/*.frag"
)

set(COMPILED_SHADERS "")

foreach(SHADER_FILE IN LISTS ALL_SHADERS)
    get_filename_component(SHADER_NAME "${SHADER_FILE}" NAME)

    set(SPIRV_FILE "${SHADER_DIR}/${SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT "${SPIRV_FILE}"
        COMMAND "${GLSLC_EXECUTABLE}" "${SHADER_FILE}" -o "${SPIRV_FILE}"
        DEPENDS "${SHADER_FILE}"
        COMMENT "Compiling shader ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS "${SPIRV_FILE}")
endforeach()

add_custom_target(engine_shaders ALL
    DEPENDS ${COMPILED_SHADERS}
)

add_dependencies(engine engine_shaders)