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
    EditorImGuiTheme.cpp
    InputSystem.cpp
    System.cpp
    CameraSystem.cpp
    CinematicCameraSystem.cpp
    GravitySimulationSystem.cpp
    JoltPhysicsSystem.cpp
    HierarchySystem.cpp
    VelocityIntegrationSystem.cpp
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
    ImGuiWindowRegistry.cpp
    ImGuiManager.cpp
    WorldSerializer.cpp
    AudioSystem.cpp
    SoundCueAsset.cpp
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
        ${cereal_SOURCE_DIR}/include
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

# ---------------------
# slangc shader compiler
# ---------------------
set(SLANGC_EXECUTABLE "" CACHE FILEPATH "Path to the Slang shader compiler")

if(NOT SLANGC_EXECUTABLE)
    if(WIN32)
        set(SLANGC_CANDIDATE "$ENV{VULKAN_SDK}/Bin/slangc.exe")
    elseif(UNIX)
        set(SLANGC_CANDIDATE "$ENV{VULKAN_SDK}/bin/slangc")
    endif()

    if(EXISTS "${SLANGC_CANDIDATE}")
        set(SLANGC_EXECUTABLE "${SLANGC_CANDIDATE}" CACHE FILEPATH "Path to the Slang shader compiler" FORCE)
    else()
        find_program(SLANGC_EXECUTABLE slangc)
    endif()
endif()

if(SLANGC_EXECUTABLE)
    message(STATUS "Using slangc: ${SLANGC_EXECUTABLE}")
else()
    message(WARNING "slangc not found. GLSL shaders will still compile, but Slang shaders are unavailable.")
endif()

set(SLANGC_COMMAND_ENV "")
if(UNIX AND NOT APPLE)
    if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
        set(SLANGC_LD_LIBRARY_PATH "$ENV{VULKAN_SDK}/lib:$ENV{LD_LIBRARY_PATH}")
    else()
        set(SLANGC_LD_LIBRARY_PATH "$ENV{VULKAN_SDK}/lib")
    endif()

    set(SLANGC_COMMAND_ENV
        "${CMAKE_COMMAND}" -E env
        "LD_LIBRARY_PATH=${SLANGC_LD_LIBRARY_PATH}"
        "PATH=$ENV{VULKAN_SDK}/bin:$ENV{PATH}"
    )
endif()

target_link_libraries(engine
    PUBLIC
        Vulkan::Vulkan
        vk-bootstrap
        glfw
        imgui
        fastgltf
        EnTT::EnTT
        nlohmann_json::nlohmann_json
        Jolt
        OpenALSoft
)

# ---------------------
# Shaders
# ---------------------
file(GLOB ALL_SHADERS CONFIGURE_DEPENDS
    "${ENGINE_SHADER_DIR}/*.comp"
    "${ENGINE_SHADER_DIR}/*.vert"
    "${ENGINE_SHADER_DIR}/*.frag"
)

file(GLOB ALL_SLANG_SHADERS CONFIGURE_DEPENDS
    "${ENGINE_SHADER_DIR}/*.comp.slang"
    "${ENGINE_SHADER_DIR}/*.vert.slang"
    "${ENGINE_SHADER_DIR}/*.frag.slang"
)

file(GLOB ALL_GRAPHICS_SLANG_SHADERS CONFIGURE_DEPENDS
    "${ENGINE_SHADER_DIR}/*.graphics.slang"
)

set(COMPILED_SHADERS "")

foreach(SHADER_FILE IN LISTS ALL_SHADERS)
    get_filename_component(SHADER_NAME "${SHADER_FILE}" NAME)

    set(SPIRV_FILE "${ENGINE_SHADER_DIR}/${SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT "${SPIRV_FILE}"
        COMMAND "${GLSLC_EXECUTABLE}" "${SHADER_FILE}" -o "${SPIRV_FILE}"
        DEPENDS "${SHADER_FILE}"
        COMMENT "Compiling shader ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS "${SPIRV_FILE}")
endforeach()

foreach(SHADER_FILE IN LISTS ALL_SLANG_SHADERS)
    get_filename_component(SHADER_NAME "${SHADER_FILE}" NAME)
    string(REGEX REPLACE "\\.slang$" "" OUTPUT_SHADER_NAME "${SHADER_NAME}")
    set(SPIRV_FILE "${ENGINE_SHADER_DIR}/${OUTPUT_SHADER_NAME}.spv")

    if(SHADER_NAME MATCHES "\\.vert\\.slang$")
        set(SLANG_STAGE "vertex")
    elseif(SHADER_NAME MATCHES "\\.frag\\.slang$")
        set(SLANG_STAGE "fragment")
    elseif(SHADER_NAME MATCHES "\\.comp\\.slang$")
        set(SLANG_STAGE "compute")
    else()
        message(FATAL_ERROR "Could not infer Slang shader stage from ${SHADER_NAME}")
    endif()

    if(NOT SLANGC_EXECUTABLE)
        message(FATAL_ERROR "Slang shader ${SHADER_NAME} requires slangc, but slangc was not found.")
    endif()

    add_custom_command(
        OUTPUT "${SPIRV_FILE}"
        COMMAND ${SLANGC_COMMAND_ENV} "${SLANGC_EXECUTABLE}" "${SHADER_FILE}" -target spirv -entry main -stage "${SLANG_STAGE}" -o "${SPIRV_FILE}"
        DEPENDS "${SHADER_FILE}"
        COMMENT "Compiling Slang shader ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS "${SPIRV_FILE}")
endforeach()

foreach(SHADER_FILE IN LISTS ALL_GRAPHICS_SLANG_SHADERS)
    get_filename_component(SHADER_NAME "${SHADER_FILE}" NAME)
    string(REGEX REPLACE "\\.graphics\\.slang$" "" OUTPUT_SHADER_NAME "${SHADER_NAME}")
    set(VERT_SPIRV_FILE "${ENGINE_SHADER_DIR}/${OUTPUT_SHADER_NAME}.vert.spv")
    set(FRAG_SPIRV_FILE "${ENGINE_SHADER_DIR}/${OUTPUT_SHADER_NAME}.frag.spv")

    if(NOT SLANGC_EXECUTABLE)
        message(FATAL_ERROR "Slang shader ${SHADER_NAME} requires slangc, but slangc was not found.")
    endif()

    add_custom_command(
        OUTPUT "${VERT_SPIRV_FILE}"
        COMMAND ${SLANGC_COMMAND_ENV} "${SLANGC_EXECUTABLE}" "${SHADER_FILE}" -target spirv -entry vertexMain -stage vertex -o "${VERT_SPIRV_FILE}"
        DEPENDS "${SHADER_FILE}"
        COMMENT "Compiling Slang vertex shader ${SHADER_NAME}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${FRAG_SPIRV_FILE}"
        COMMAND ${SLANGC_COMMAND_ENV} "${SLANGC_EXECUTABLE}" "${SHADER_FILE}" -target spirv -entry fragmentMain -stage fragment -o "${FRAG_SPIRV_FILE}"
        DEPENDS "${SHADER_FILE}"
        COMMENT "Compiling Slang fragment shader ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS "${VERT_SPIRV_FILE}")
    list(APPEND COMPILED_SHADERS "${FRAG_SPIRV_FILE}")
endforeach()

add_custom_target(engine_shaders ALL
    DEPENDS ${COMPILED_SHADERS}
)

add_dependencies(engine engine_shaders)
