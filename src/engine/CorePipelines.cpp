#include "Core.h"
#include "vk_pipelines.h"
#include "vk_initializers.h"

#include <cassert>

namespace Engine {
RenderPipelineId Core::RegisterRenderPipeline(std::string name, MaterialPipeline pipeline)
{
    if (auto existing = _renderPipelineIdsByName.find(name); existing != _renderPipelineIdsByName.end()) {
        _renderPipelines[existing->second.value] = pipeline;
        return existing->second;
    }

    RenderPipelineId id{ static_cast<uint32_t>(_renderPipelines.size()) };
    _renderPipelines.push_back(pipeline);
    _renderPipelineIdsByName.emplace(std::move(name), id);
    return id;
}

RenderPipelineId Core::FindRenderPipeline(std::string_view name) const
{
    auto it = _renderPipelineIdsByName.find(std::string{ name });
    if (it == _renderPipelineIdsByName.end()) {
        return {};
    }

    return it->second;
}

const MaterialPipeline& Core::GetRenderPipeline(RenderPipelineId id) const
{
    assert(id.IsValid());
    assert(id.value < _renderPipelines.size());
    return _renderPipelines[id.value];
}

RenderPipelineId Core::ResolveEditorWireframePipeline(RenderPipelineId pipelineId) const
{
    if (!_editorWireframeEnabled || !pipelineId.IsValid()) {
        return pipelineId;
    }

    if (pipelineId == _meshPipelineId && _meshWireframePipelineId.IsValid()) {
        return _meshWireframePipelineId;
    }
    if (pipelineId == _instancedMeshPipelineId && _instancedMeshWireframePipelineId.IsValid()) {
        return _instancedMeshWireframePipelineId;
    }
    if (pipelineId == _transparentMeshPipelineId && _transparentMeshWireframePipelineId.IsValid()) {
        return _transparentMeshWireframePipelineId;
    }
    if (pipelineId == _transparentInstancedMeshPipelineId &&
        _transparentInstancedMeshWireframePipelineId.IsValid()) {
        return _transparentInstancedMeshWireframePipelineId;
    }

    return pipelineId;
}

bool Core::LoadEngineShaderModule(const std::filesystem::path& path, VkShaderModule* outShaderModule)
{
    const auto resolvedPath = ResolveEnginePath(path);
    const auto shaderPath = resolvedPath.string();
    return vkutil::load_shader_module(shaderPath.c_str(), _device, outShaderModule);
}

bool Core::LoadProjectShaderModule(const std::filesystem::path& path, VkShaderModule* outShaderModule)
{
    const auto resolvedPath = ResolveProjectPath(path);
    const auto shaderPath = resolvedPath.string();
    return vkutil::load_shader_module(shaderPath.c_str(), _device, outShaderModule);
}

VkPipeline Core::BuildMeshGraphicsPipeline(
    VkPipelineLayout layout,
    VkShaderModule vertexShader,
    VkShaderModule fragmentShader,
    bool transparent,
    VkPolygonMode polygonMode)
{
    PipelineBuilder pipelineBuilder;

    pipelineBuilder._pipelineLayout = layout;
    pipelineBuilder.set_shaders(vertexShader, fragmentShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(polygonMode);
    //pipelineBuilder.set_cull_mode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pipelineBuilder.set_multisampling(_msaaSamples);
    if (transparent) {
        pipelineBuilder.enable_blending_alphablend();
    }
    else {
        pipelineBuilder.disable_blending();
    }
    pipelineBuilder.disable_depthtest();
    pipelineBuilder.enable_depthtest(!transparent, VK_COMPARE_OP_GREATER_OR_EQUAL);
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_depthImage.imageFormat);

    return pipelineBuilder.build_pipeline(_device);
}

MaterialPipelineSet Core::RegisterMeshMaterialPipelineSet(
    std::string name,
    const std::filesystem::path& fragmentShaderPath)
{
    assert(_meshPipelineLayout != VK_NULL_HANDLE);
    assert(_instancedMeshPipelineLayout != VK_NULL_HANDLE);

    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    if (!LoadProjectShaderModule(fragmentShaderPath, &fragmentShader)) {
        ENGINE_LOG_ERROR("Failed to load project mesh fragment shader: " + fragmentShaderPath.generic_string());
        return {};
    }

    VkShaderModule singleVertexShader = VK_NULL_HANDLE;
    if (!LoadEngineShaderModule("shaders/colored_triangle_mesh.vert.spv", &singleVertexShader)) {
        ENGINE_LOG_ERROR("Failed to load engine mesh vertex shader for project material pipeline");
        vkDestroyShaderModule(_device, fragmentShader, nullptr);
        return {};
    }

    VkShaderModule instancedVertexShader = VK_NULL_HANDLE;
    if (!LoadEngineShaderModule("shaders/batch_color_mesh.vert.spv", &instancedVertexShader)) {
        ENGINE_LOG_ERROR("Failed to load engine instanced mesh vertex shader for project material pipeline");
        vkDestroyShaderModule(_device, singleVertexShader, nullptr);
        vkDestroyShaderModule(_device, fragmentShader, nullptr);
        return {};
    }

    const VkPipeline singlePipeline =
        BuildMeshGraphicsPipeline(_meshPipelineLayout, singleVertexShader, fragmentShader);
    const VkPipeline instancedPipeline =
        BuildMeshGraphicsPipeline(_instancedMeshPipelineLayout, instancedVertexShader, fragmentShader);
    const VkPipeline wireframeSinglePipeline =
        BuildMeshGraphicsPipeline(
            _meshPipelineLayout,
            singleVertexShader,
            fragmentShader,
            false,
            VK_POLYGON_MODE_LINE);
    const VkPipeline wireframeInstancedPipeline =
        BuildMeshGraphicsPipeline(
            _instancedMeshPipelineLayout,
            instancedVertexShader,
            fragmentShader,
            false,
            VK_POLYGON_MODE_LINE);

    vkDestroyShaderModule(_device, instancedVertexShader, nullptr);
    vkDestroyShaderModule(_device, singleVertexShader, nullptr);
    vkDestroyShaderModule(_device, fragmentShader, nullptr);

    auto singleId = RegisterRenderPipeline(
        name + ".Single",
        MaterialPipeline{
            .pipeline = singlePipeline,
            .layout = _meshPipelineLayout
        }
    );

    auto instancedId = RegisterRenderPipeline(
        name + ".Instanced",
        MaterialPipeline{
            .pipeline = instancedPipeline,
            .layout = _instancedMeshPipelineLayout
        }
    );

    auto wireframeSingleId = RegisterRenderPipeline(
        name + ".WireframeSingle",
        MaterialPipeline{
            .pipeline = wireframeSinglePipeline,
            .layout = _meshPipelineLayout
        }
    );

    auto wireframeInstancedId = RegisterRenderPipeline(
        name + ".WireframeInstanced",
        MaterialPipeline{
            .pipeline = wireframeInstancedPipeline,
            .layout = _instancedMeshPipelineLayout
        }
    );

    _mainDeletionQueue.push_function([
        this,
        singlePipeline,
        instancedPipeline,
        wireframeSinglePipeline,
        wireframeInstancedPipeline]() {
        vkDestroyPipeline(_device, singlePipeline, nullptr);
        vkDestroyPipeline(_device, instancedPipeline, nullptr);
        vkDestroyPipeline(_device, wireframeSinglePipeline, nullptr);
        vkDestroyPipeline(_device, wireframeInstancedPipeline, nullptr);
    });

    return MaterialPipelineSet{
        .single = singleId,
        .instanced = instancedId,
        .wireframeSingle = wireframeSingleId,
        .wireframeInstanced = wireframeInstancedId
    };
}

void Core::InitBackgroundPipelines()
{
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
    computeLayout.setLayoutCount = 1;

    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(ComputePushConstants) ;
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    computeLayout.pPushConstantRanges = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

    VkShaderModule gradientShader;
    if (!LoadEngineShaderModule("shaders/gradient.comp.spv", &gradientShader)) {
        ENGINE_LOG_ERROR("Error when building the compute shader");
    }


    VkShaderModule skyShader;
    if (!LoadEngineShaderModule("shaders/sky.comp.spv", &skyShader)) {
        ENGINE_LOG_ERROR("Error when building the compute shader");
    }

    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageinfo.module = gradientShader;
    stageinfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = _gradientPipelineLayout;
    computePipelineCreateInfo.stage = stageinfo;

    ComputeEffect gradient;
    gradient.layout = _gradientPipelineLayout;
    gradient.name = "gradient";
    gradient.data = {};

    //default colors
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);
            
    VK_CHECK(vkCreateComputePipelines(_device,VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &gradient.pipeline));

    //change the shader module only to create the sky shader
    computePipelineCreateInfo.stage.module = skyShader;

    ComputeEffect sky;
    sky.layout = _gradientPipelineLayout;
    sky.name = "sky";
    sky.data = {};
    //default sky parameters
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4 ,0.97);

    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

    _backgroundEffects.push_back(gradient);
    _backgroundEffects.push_back(sky);


    vkDestroyShaderModule(_device, gradientShader, nullptr);
    vkDestroyShaderModule(_device, skyShader, nullptr);
    
    VkPipeline gradientPipeline = gradient.pipeline;
    VkPipeline skyPipeline = sky.pipeline;
    VkPipelineLayout layout = _gradientPipelineLayout;

    _mainDeletionQueue.push_function([this, gradientPipeline, skyPipeline, layout]() {
        vkDestroyPipeline(_device, gradientPipeline, nullptr);
        vkDestroyPipeline(_device, skyPipeline, nullptr);
        vkDestroyPipelineLayout(_device, layout, nullptr);
    });
}

void Core::InitMeshPipeline() {
    assert(_multiImageDescriptorLayout != VK_NULL_HANDLE);
    assert(_gpuSceneDataDescriptorLayout != VK_NULL_HANDLE);
    VkShaderModule triangleFragShader;
    if (!LoadEngineShaderModule("shaders/colored_triangle.frag.spv", &triangleFragShader))
        ENGINE_LOG_ERROR("Error when building the triangle fragment shader module");

    VkShaderModule triangleVertexShader;
    if (!LoadEngineShaderModule("shaders/colored_triangle_mesh.vert.spv", &triangleVertexShader))
        ENGINE_LOG_ERROR("Error when building the triangle vertex shader module");

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(GPUDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayout setLayouts[] = {
        _multiImageDescriptorLayout,      // set 0
        _gpuSceneDataDescriptorLayout,    // set 1
        _environmentDescriptorLayout,     // set 2
        _shadowDescriptorLayout           // set 3
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &bufferRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = setLayouts;
    pipeline_layout_info.setLayoutCount = 4;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_meshPipelineLayout));

    //finally build the pipeline
    _meshPipeline = BuildMeshGraphicsPipeline(_meshPipelineLayout, triangleVertexShader, triangleFragShader);
    _meshWireframePipeline =
        BuildMeshGraphicsPipeline(
            _meshPipelineLayout,
            triangleVertexShader,
            triangleFragShader,
            false,
            VK_POLYGON_MODE_LINE);
    _meshPipelineId = RegisterRenderPipeline(
        "Engine/MeshPBR",
        MaterialPipeline{
            .pipeline = _meshPipeline,
            .layout = _meshPipelineLayout
        }
    );
    _meshWireframePipelineId = RegisterRenderPipeline(
        "Engine/MeshPBR.Wireframe",
        MaterialPipeline{
            .pipeline = _meshWireframePipeline,
            .layout = _meshPipelineLayout
        }
    );

    //clean structures
    vkDestroyShaderModule(_device, triangleFragShader, nullptr);
    vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _meshWireframePipeline, nullptr);
        vkDestroyPipeline(_device, _meshPipeline, nullptr);
    });
}

void Core::InitInstancedMeshPipeline() {
    VkShaderModule triangleFragShader;
    if (!LoadEngineShaderModule("shaders/colored_triangle.frag.spv", &triangleFragShader))
        ENGINE_LOG_ERROR("Error when building the triangle fragment shader module");


    VkShaderModule triangleVertexShader;
    if (!LoadEngineShaderModule("shaders/batch_color_mesh.vert.spv", &triangleVertexShader))
        ENGINE_LOG_ERROR("Error when building the triangle vertex shader module");

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(BatchDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayout setLayouts[] = {
        _multiImageDescriptorLayout,      // set 0
        _gpuSceneDataDescriptorLayout,    // set 1
        _environmentDescriptorLayout,     // set 2
        _shadowDescriptorLayout           // set 3
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &bufferRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = setLayouts;
    pipeline_layout_info.setLayoutCount = 4;

    VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_instancedMeshPipelineLayout));

    //finally build the pipeline
    _instancedMeshPipeline = BuildMeshGraphicsPipeline(_instancedMeshPipelineLayout, triangleVertexShader, triangleFragShader);
    _instancedMeshWireframePipeline =
        BuildMeshGraphicsPipeline(
            _instancedMeshPipelineLayout,
            triangleVertexShader,
            triangleFragShader,
            false,
            VK_POLYGON_MODE_LINE);
    _instancedMeshPipelineId = RegisterRenderPipeline(
        "Engine/InstancedMeshPBR",
        MaterialPipeline{
            .pipeline = _instancedMeshPipeline,
            .layout = _instancedMeshPipelineLayout
        }
    );
    _instancedMeshWireframePipelineId = RegisterRenderPipeline(
        "Engine/InstancedMeshPBR.Wireframe",
        MaterialPipeline{
            .pipeline = _instancedMeshWireframePipeline,
            .layout = _instancedMeshPipelineLayout
        }
    );

    //clean structures
    vkDestroyShaderModule(_device, triangleFragShader, nullptr);
    vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _instancedMeshPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _instancedMeshWireframePipeline, nullptr);
        vkDestroyPipeline(_device, _instancedMeshPipeline, nullptr);
    });
}

void Core::InitTransparentMeshPipeline()
{
    assert(_meshPipelineLayout != VK_NULL_HANDLE);
    assert(_instancedMeshPipelineLayout != VK_NULL_HANDLE);

    VkShaderModule triangleFragShader;
    if (!LoadEngineShaderModule("shaders/colored_triangle.frag.spv", &triangleFragShader)) {
        ENGINE_LOG_ERROR("Error when building the transparent mesh fragment shader module");
    }

    VkShaderModule singleVertexShader;
    if (!LoadEngineShaderModule("shaders/colored_triangle_mesh.vert.spv", &singleVertexShader)) {
        ENGINE_LOG_ERROR("Error when building the transparent mesh vertex shader module");
    }

    VkShaderModule instancedVertexShader;
    if (!LoadEngineShaderModule("shaders/batch_color_mesh.vert.spv", &instancedVertexShader)) {
        ENGINE_LOG_ERROR("Error when building the transparent instanced mesh vertex shader module");
    }

    VkPipeline transparentMeshPipeline =
        BuildMeshGraphicsPipeline(_meshPipelineLayout, singleVertexShader, triangleFragShader, true);
    VkPipeline transparentInstancedMeshPipeline =
        BuildMeshGraphicsPipeline(_instancedMeshPipelineLayout, instancedVertexShader, triangleFragShader, true);
    _transparentMeshWireframePipeline =
        BuildMeshGraphicsPipeline(
            _meshPipelineLayout,
            singleVertexShader,
            triangleFragShader,
            true,
            VK_POLYGON_MODE_LINE);
    _transparentInstancedMeshWireframePipeline =
        BuildMeshGraphicsPipeline(
            _instancedMeshPipelineLayout,
            instancedVertexShader,
            triangleFragShader,
            true,
            VK_POLYGON_MODE_LINE);

    _transparentMeshPipelineId = RegisterRenderPipeline(
        "Engine/TransparentMeshPBR",
        MaterialPipeline{
            .pipeline = transparentMeshPipeline,
            .layout = _meshPipelineLayout
        }
    );

    _transparentInstancedMeshPipelineId = RegisterRenderPipeline(
        "Engine/TransparentInstancedMeshPBR",
        MaterialPipeline{
            .pipeline = transparentInstancedMeshPipeline,
            .layout = _instancedMeshPipelineLayout
        }
    );

    _transparentMeshWireframePipelineId = RegisterRenderPipeline(
        "Engine/TransparentMeshPBR.Wireframe",
        MaterialPipeline{
            .pipeline = _transparentMeshWireframePipeline,
            .layout = _meshPipelineLayout
        }
    );

    _transparentInstancedMeshWireframePipelineId = RegisterRenderPipeline(
        "Engine/TransparentInstancedMeshPBR.Wireframe",
        MaterialPipeline{
            .pipeline = _transparentInstancedMeshWireframePipeline,
            .layout = _instancedMeshPipelineLayout
        }
    );

    vkDestroyShaderModule(_device, instancedVertexShader, nullptr);
    vkDestroyShaderModule(_device, singleVertexShader, nullptr);
    vkDestroyShaderModule(_device, triangleFragShader, nullptr);

    _mainDeletionQueue.push_function([this, transparentMeshPipeline, transparentInstancedMeshPipeline]() {
        vkDestroyPipeline(_device, _transparentInstancedMeshWireframePipeline, nullptr);
        vkDestroyPipeline(_device, _transparentMeshWireframePipeline, nullptr);
        vkDestroyPipeline(_device, transparentInstancedMeshPipeline, nullptr);
        vkDestroyPipeline(_device, transparentMeshPipeline, nullptr);
    });
}

void Core::InitEffectMeshPipeline()
{
    VkShaderModule fragShader;
    if (!LoadEngineShaderModule("shaders/effect_mesh.frag.spv", &fragShader)) {
        ENGINE_LOG_ERROR("Error when building the effect mesh fragment shader module");
    }

    VkShaderModule vertexShader;
    if (!LoadEngineShaderModule("shaders/effect_mesh.vert.spv", &vertexShader)) {
        ENGINE_LOG_ERROR("Error when building the effect mesh vertex shader module");
    }

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(EffectMeshPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
    pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_effectMeshPipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _effectMeshPipelineLayout;
    pipelineBuilder.set_shaders(vertexShader, fragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling(_msaaSamples);
    pipelineBuilder.enable_blending_alphablend();
    pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_depthImage.imageFormat);

    _effectMeshPipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, fragShader, nullptr);
    vkDestroyShaderModule(_device, vertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _effectMeshPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _effectMeshPipeline, nullptr);
    });
}

void Core::InitLinePipeline()
{
    VkShaderModule fragShader;
    if (!LoadEngineShaderModule("shaders/line.frag.spv", &fragShader)) {
        ENGINE_LOG_ERROR("Error when building the line fragment shader module");
    }

    VkShaderModule vertexShader;
    if (!LoadEngineShaderModule("shaders/line.vert.spv", &vertexShader)) {
        ENGINE_LOG_ERROR("Error when building the line vertex shader module");
    }

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(LineDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
    pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_linePipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _linePipelineLayout;
    pipelineBuilder.set_shaders(vertexShader, fragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling(_msaaSamples);
    pipelineBuilder.enable_blending_alphablend();
    pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_depthImage.imageFormat);

    _linePipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, fragShader, nullptr);
    vkDestroyShaderModule(_device, vertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _linePipelineLayout, nullptr);
        vkDestroyPipeline(_device, _linePipeline, nullptr);
    });
}

void Core::InitHeightFogPipeline()
{
    VkShaderModule fullscreenVertexShader = VK_NULL_HANDLE;
    if (!LoadEngineShaderModule("shaders/fullscreen_triangle.vert.spv", &fullscreenVertexShader)) {
        ENGINE_LOG_ERROR("Error when building the height fog fullscreen vertex shader module");
        return;
    }

    VkShaderModule fogFragShader = VK_NULL_HANDLE;
    if (!LoadProjectShaderModule("shaders/height_fog.frag.spv", &fogFragShader)) {
        vkDestroyShaderModule(_device, fullscreenVertexShader, nullptr);
        return;
    }

    VkShaderModule fogMsaaFragShader = VK_NULL_HANDLE;
    if (!LoadProjectShaderModule("shaders/height_fog_msaa.frag.spv", &fogMsaaFragShader)) {
        vkDestroyShaderModule(_device, fogFragShader, nullptr);
        vkDestroyShaderModule(_device, fullscreenVertexShader, nullptr);
        return;
    }

    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(HeightFogPushConstants);
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_sampledImageDescriptorLayout;
    pipelineLayoutInfo.setLayoutCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_heightFogPipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _heightFogPipelineLayout;
    pipelineBuilder.set_shaders(fullscreenVertexShader, fogFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.enable_blending_alphablend();
    pipelineBuilder.disable_depthtest();
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);

    _heightFogPipeline = pipelineBuilder.build_pipeline(_device);

    pipelineBuilder.set_shaders(fullscreenVertexShader, fogMsaaFragShader);
    _heightFogMsaaPipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, fogMsaaFragShader, nullptr);
    vkDestroyShaderModule(_device, fogFragShader, nullptr);
    vkDestroyShaderModule(_device, fullscreenVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _heightFogPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _heightFogPipeline, nullptr);
        vkDestroyPipeline(_device, _heightFogMsaaPipeline, nullptr);
    });
}

void Core::InitDepthVisualizationPipeline()
{
    VkShaderModule fullscreenVertexShader;
    if (!LoadEngineShaderModule("shaders/fullscreen_triangle.vert.spv", &fullscreenVertexShader)) {
        ENGINE_LOG_ERROR("Error when building the depth visualization fullscreen vertex shader module");
    }

    VkShaderModule depthFragShader;
    if (!LoadEngineShaderModule("shaders/depth_visualization.frag.spv", &depthFragShader)) {
        ENGINE_LOG_ERROR("Error when building the depth visualization fragment shader module");
    }

    VkShaderModule depthMsaaFragShader;
    if (!LoadEngineShaderModule("shaders/depth_visualization_msaa.frag.spv", &depthMsaaFragShader)) {
        ENGINE_LOG_ERROR("Error when building the depth visualization MSAA fragment shader module");
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
    pipelineLayoutInfo.pSetLayouts = &_sampledImageDescriptorLayout;
    pipelineLayoutInfo.setLayoutCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_depthVisualizationPipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _depthVisualizationPipelineLayout;
    pipelineBuilder.set_shaders(fullscreenVertexShader, depthFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.disable_depthtest();
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);

    _depthVisualizationPipeline = pipelineBuilder.build_pipeline(_device);

    pipelineBuilder.set_shaders(fullscreenVertexShader, depthMsaaFragShader);
    _depthVisualizationMsaaPipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, depthMsaaFragShader, nullptr);
    vkDestroyShaderModule(_device, depthFragShader, nullptr);
    vkDestroyShaderModule(_device, fullscreenVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _depthVisualizationPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _depthVisualizationPipeline, nullptr);
        vkDestroyPipeline(_device, _depthVisualizationMsaaPipeline, nullptr);
    });
}

void Core::InitScreenPostProcessMaskPipeline()
{
    VkShaderModule vertexShader;
    if (!LoadEngineShaderModule("shaders/screen_post_process_mask.vert.spv", &vertexShader)) {
        ENGINE_LOG_ERROR("Error when building the screen post-process mask vertex shader module");
    }

    VkShaderModule fragmentShader;
    if (!LoadEngineShaderModule("shaders/screen_post_process_mask.frag.spv", &fragmentShader)) {
        ENGINE_LOG_ERROR("Error when building the screen post-process mask fragment shader module");
    }

    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(ScreenPostProcessMaskPushConstants);
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_screenPostProcessMaskPipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _screenPostProcessMaskPipelineLayout;
    pipelineBuilder.set_shaders(vertexShader, fragmentShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.disable_depthtest();
    pipelineBuilder.set_color_attachment_format(_screenPostProcessMaskImage.imageFormat);

    _screenPostProcessMaskPipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, fragmentShader, nullptr);
    vkDestroyShaderModule(_device, vertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _screenPostProcessMaskPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _screenPostProcessMaskPipeline, nullptr);
    });
}

void Core::InitScreenMaskBlurPipeline()
{
    VkShaderModule fullscreenVertexShader;
    if (!LoadEngineShaderModule("shaders/fullscreen_triangle.vert.spv", &fullscreenVertexShader)) {
        ENGINE_LOG_ERROR("Error when building the screen mask blur vertex shader module");
    }

    VkShaderModule fragmentShader;
    if (!LoadEngineShaderModule("shaders/screen_mask_blur.frag.spv", &fragmentShader)) {
        ENGINE_LOG_ERROR("Error when building the screen mask blur fragment shader module");
    }

    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(ScreenMaskBlurPushConstants);
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_singleImageDescriptorLayout;
    pipelineLayoutInfo.setLayoutCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_screenMaskBlurPipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _screenMaskBlurPipelineLayout;
    pipelineBuilder.set_shaders(fullscreenVertexShader, fragmentShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.disable_depthtest();
    pipelineBuilder.set_color_attachment_format(_screenPostProcessBlurredMaskImage.imageFormat);

    _screenMaskBlurPipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, fragmentShader, nullptr);
    vkDestroyShaderModule(_device, fullscreenVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _screenMaskBlurPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _screenMaskBlurPipeline, nullptr);
    });
}

void Core::InitScreenPostProcessPipeline()
{
    VkShaderModule fullscreenVertexShader;
    if (!LoadEngineShaderModule("shaders/fullscreen_triangle.vert.spv", &fullscreenVertexShader)) {
        ENGINE_LOG_ERROR("Error when building the screen corruption fullscreen vertex shader module");
    }

    VkShaderModule fragShader = VK_NULL_HANDLE;
    if (!LoadProjectShaderModule("shaders/project_screen_effect.frag.spv", &fragShader)) {
        return;
    }

    VkShaderModule msaaFragShader = VK_NULL_HANDLE;
    if (!LoadProjectShaderModule("shaders/project_screen_effect_msaa.frag.spv", &msaaFragShader)) {
        vkDestroyShaderModule(_device, fragShader, nullptr);
        return;
    }

    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(ScreenPostProcessPushConstants);
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_screenPostProcessDescriptorLayout;
    pipelineLayoutInfo.setLayoutCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_screenPostProcessPipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _screenPostProcessPipelineLayout;
    pipelineBuilder.set_shaders(fullscreenVertexShader, fragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.disable_depthtest();
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);

    _screenPostProcessPipeline = pipelineBuilder.build_pipeline(_device);

    pipelineBuilder.set_shaders(fullscreenVertexShader, msaaFragShader);
    _screenPostProcessMsaaPipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, msaaFragShader, nullptr);
    vkDestroyShaderModule(_device, fragShader, nullptr);
    vkDestroyShaderModule(_device, fullscreenVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _screenPostProcessPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _screenPostProcessPipeline, nullptr);
        vkDestroyPipeline(_device, _screenPostProcessMsaaPipeline, nullptr);
    });
}

void Core::InitShadowPipeline()
{
    VkShaderModule fragShader;
    if (!LoadEngineShaderModule("shaders/shadow_depth.frag.spv", &fragShader)) {
        ENGINE_LOG_ERROR("Error when building the shadow fragment shader module");
    }

    VkShaderModule vertexShader;
    if (!LoadEngineShaderModule("shaders/shadow_depth.vert.spv", &vertexShader)) {
        ENGINE_LOG_ERROR("Error when building the shadow vertex shader module");
    }

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(ShadowDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
    pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_shadowPipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _shadowPipelineLayout;
    pipelineBuilder.set_shaders(vertexShader, fragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_depth_bias(-1.25f, -1.75f);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    pipelineBuilder.set_no_color_attachment();
    pipelineBuilder.set_depth_format(_shadowMapImage.imageFormat);

    _shadowPipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, fragShader, nullptr);
    vkDestroyShaderModule(_device, vertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _shadowPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _shadowPipeline, nullptr);
    });
}

void Core::InitSelectionOutlinePipeline()
{
    VkShaderModule maskFragShader;
    if (!LoadEngineShaderModule("shaders/selection_mask.frag.spv", &maskFragShader)) {
        ENGINE_LOG_ERROR("Error when building the selection mask fragment shader module");
    }

    VkShaderModule maskVertexShader;
    if (!LoadEngineShaderModule("shaders/selection_mask.vert.spv", &maskVertexShader)) {
        ENGINE_LOG_ERROR("Error when building the selection mask vertex shader module");
    }

    VkPushConstantRange maskPushRange{};
    maskPushRange.offset = 0;
    maskPushRange.size = sizeof(SelectionMaskPushConstants);
    maskPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo maskLayoutInfo = vkinit::pipeline_layout_create_info();
    maskLayoutInfo.pPushConstantRanges = &maskPushRange;
    maskLayoutInfo.pushConstantRangeCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &maskLayoutInfo, nullptr, &_selectionMaskPipelineLayout));

    PipelineBuilder maskBuilder;
    maskBuilder._pipelineLayout = _selectionMaskPipelineLayout;
    maskBuilder.set_shaders(maskVertexShader, maskFragShader);
    maskBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    maskBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    maskBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    maskBuilder.set_multisampling_none();
    maskBuilder.disable_blending();
    maskBuilder.disable_depthtest();
    maskBuilder.set_color_attachment_format(_selectionMaskImage.imageFormat);

    _selectionMaskPipeline = maskBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, maskFragShader, nullptr);
    vkDestroyShaderModule(_device, maskVertexShader, nullptr);

    VkShaderModule outlineFragShader;
    if (!LoadEngineShaderModule("shaders/selection_outline.frag.spv", &outlineFragShader)) {
        ENGINE_LOG_ERROR("Error when building the selection outline fragment shader module");
    }

    VkShaderModule fullscreenVertexShader;
    if (!LoadEngineShaderModule("shaders/fullscreen_triangle.vert.spv", &fullscreenVertexShader)) {
        ENGINE_LOG_ERROR("Error when building the fullscreen triangle vertex shader module");
    }

    VkPushConstantRange outlinePushRange{};
    outlinePushRange.offset = 0;
    outlinePushRange.size = sizeof(SelectionOutlinePushConstants);
    outlinePushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo outlineLayoutInfo = vkinit::pipeline_layout_create_info();
    outlineLayoutInfo.pPushConstantRanges = &outlinePushRange;
    outlineLayoutInfo.pushConstantRangeCount = 1;
    outlineLayoutInfo.pSetLayouts = &_singleImageDescriptorLayout;
    outlineLayoutInfo.setLayoutCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &outlineLayoutInfo, nullptr, &_selectionOutlinePipelineLayout));

    PipelineBuilder outlineBuilder;
    outlineBuilder._pipelineLayout = _selectionOutlinePipelineLayout;
    outlineBuilder.set_shaders(fullscreenVertexShader, outlineFragShader);
    outlineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    outlineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    outlineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    outlineBuilder.set_multisampling_none();
    outlineBuilder.enable_blending_alphablend();
    outlineBuilder.disable_depthtest();
    outlineBuilder.set_color_attachment_format(_drawImage.imageFormat);

    _selectionOutlinePipeline = outlineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, outlineFragShader, nullptr);
    vkDestroyShaderModule(_device, fullscreenVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _selectionMaskPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _selectionMaskPipeline, nullptr);
        vkDestroyPipelineLayout(_device, _selectionOutlinePipelineLayout, nullptr);
        vkDestroyPipeline(_device, _selectionOutlinePipeline, nullptr);
    });
}

void Core::InitSkyboxPipeline()
{
    VkShaderModule skyboxFragShader;
    if (!LoadEngineShaderModule("shaders/skybox.frag.spv", &skyboxFragShader))
        ENGINE_LOG_ERROR("Error when building the skybox fragment shader module");

    VkShaderModule skyboxVertexShader;
    if (!LoadEngineShaderModule("shaders/skybox.vert.spv", &skyboxVertexShader))
        ENGINE_LOG_ERROR("Error when building the skybox vertex shader module");

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(glm::mat4);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipeline_layout_info =
        vkinit::pipeline_layout_create_info();

    pipeline_layout_info.pPushConstantRanges = &bufferRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = &_skyboxDescriptorLayout;
    pipeline_layout_info.setLayoutCount = 1;

    VK_CHECK(vkCreatePipelineLayout(
        _device,
        &pipeline_layout_info,
        nullptr,
        &_skyboxPipelineLayout
    ));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _skyboxPipelineLayout;

    pipelineBuilder.set_shaders(skyboxVertexShader, skyboxFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling(_msaaSamples);
    pipelineBuilder.disable_blending();

    // Skybox: depth test on, but depth write should ideally be off.
    pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_depthImage.imageFormat);

    _skyboxPipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, skyboxFragShader, nullptr);
    vkDestroyShaderModule(_device, skyboxVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _skyboxPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _skyboxPipeline, nullptr);
    });
}

void Core::InitPrefilterPipeline()
{
    VkShaderModule fragShader;
    if (!LoadEngineShaderModule("shaders/prefilter.frag.spv", &fragShader)) {
        ENGINE_LOG_ERROR("Error when building prefilter fragment shader module");
    }

    VkShaderModule vertShader;
    if (!LoadEngineShaderModule("shaders/prefilter.vert.spv", &vertShader)) {
        ENGINE_LOG_ERROR("Error when building prefilter vertex shader module");
    }

    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(PrefilterPushConstants);
    pushRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo =
        vkinit::pipeline_layout_create_info();

    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_skyboxDescriptorLayout;
    pipelineLayoutInfo.setLayoutCount = 1;

    VK_CHECK(vkCreatePipelineLayout(
        _device,
        &pipelineLayoutInfo,
        nullptr,
        &_prefilterPipelineLayout
    ));

    PipelineBuilder pipelineBuilder;

    pipelineBuilder._pipelineLayout = _prefilterPipelineLayout;

    pipelineBuilder.set_shaders(vertShader, fragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();

    pipelineBuilder.disable_depthtest();

    pipelineBuilder.set_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT);

    _prefilterPipeline =
        pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, fragShader, nullptr);
    vkDestroyShaderModule(_device, vertShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _prefilterPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _prefilterPipeline, nullptr);
    });
}

void Core::InitIrradiancePipeline()
{
    VkShaderModule fragShader;
    LoadEngineShaderModule("shaders/irradiance.frag.spv", &fragShader);

    VkShaderModule vertShader;
    LoadEngineShaderModule("shaders/prefilter.vert.spv", &vertShader);

    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(PrefilterPushConstants);
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo =
        vkinit::pipeline_layout_create_info();

    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_skyboxDescriptorLayout;
    pipelineLayoutInfo.setLayoutCount = 1;

    VK_CHECK(vkCreatePipelineLayout(
        _device,
        &pipelineLayoutInfo,
        nullptr,
        &_irradiancePipelineLayout
    ));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _irradiancePipelineLayout;

    pipelineBuilder.set_shaders(vertShader, fragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.disable_depthtest();
    pipelineBuilder.set_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT);

    _irradiancePipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, fragShader, nullptr);
    vkDestroyShaderModule(_device, vertShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _irradiancePipelineLayout, nullptr);
        vkDestroyPipeline(_device, _irradiancePipeline, nullptr);
    });
}

void Core::InitBRDFLUTPipeline()
{
        VkShaderModule fragShader;
    if (!LoadEngineShaderModule("shaders/brdf_lut.frag.spv", &fragShader)) {
        ENGINE_LOG_ERROR("Error when building BRDF LUT fragment shader module");
    }

    VkShaderModule vertShader;
    if (!LoadEngineShaderModule("shaders/fullscreen_triangle.vert.spv", &vertShader)) {
        ENGINE_LOG_ERROR("Error when building fullscreen triangle vertex shader module");
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo =
        vkinit::pipeline_layout_create_info();

    pipelineLayoutInfo.pPushConstantRanges = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.setLayoutCount = 0;

    VK_CHECK(vkCreatePipelineLayout(
        _device,
        &pipelineLayoutInfo,
        nullptr,
        &_brdfLUTPipelineLayout
    ));

    PipelineBuilder pipelineBuilder;

    pipelineBuilder._pipelineLayout = _brdfLUTPipelineLayout;

    pipelineBuilder.set_shaders(vertShader, fragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();

    pipelineBuilder.disable_depthtest();

    pipelineBuilder.set_color_attachment_format(VK_FORMAT_R16G16_SFLOAT);

    _brdfLUTPipeline =
        pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, fragShader, nullptr);
    vkDestroyShaderModule(_device, vertShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _brdfLUTPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _brdfLUTPipeline, nullptr);
    });
}

} // end of namespace engine
