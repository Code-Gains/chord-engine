#include "Core.h"
#include "vk_pipelines.h"
#include "vk_initializers.h"

namespace Engine {
bool Core::LoadEngineShaderModule(const std::filesystem::path& path, VkShaderModule* outShaderModule)
{
    const auto resolvedPath = ResolveEnginePath(path);
    const auto shaderPath = resolvedPath.string();
    return vkutil::load_shader_module(shaderPath.c_str(), _device, outShaderModule);
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
    // if (!vkutil::load_shader_module("../../../shaders/gradient_color.comp.spv", _device, &gradientShader)) {
    //     ENGINE_LOG_ERROR("Error when building the compute shader");
    // }
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
    // if (!vkutil::load_shader_module("../../../shaders/batch_color_mesh.vert.spv", _device, &triangleVertexShader))
    //     ENGINE_LOG_ERROR("Error when building the triangle vertex shader module");

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(GPUDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayout setLayouts[] = {
        _multiImageDescriptorLayout,      // set 0
        _gpuSceneDataDescriptorLayout,    // set 1
        _environmentDescriptorLayout      // set 2
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &bufferRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = setLayouts;
    pipeline_layout_info.setLayoutCount = 3;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_meshPipelineLayout));

    PipelineBuilder pipelineBuilder;

    //use the triangle layout we created
    pipelineBuilder._pipelineLayout = _meshPipelineLayout;
    //connecting the vertex and pixel shaders to the pipeline
    pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
    //it will draw triangles
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    //filled triangles
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    //no backface culling
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    //no multisampling
    pipelineBuilder.set_multisampling_none();
    //no blending
    pipelineBuilder.disable_blending();
    //pipelineBuilder.enable_blending_additive();

    pipelineBuilder.disable_depthtest();
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    //connect the image format we will draw into, from draw image
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_depthImage.imageFormat);

    //finally build the pipeline
    _meshPipeline = pipelineBuilder.build_pipeline(_device);

    //clean structures
    vkDestroyShaderModule(_device, triangleFragShader, nullptr);
    vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
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
        _environmentDescriptorLayout      // set 2
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &bufferRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = setLayouts;
    pipeline_layout_info.setLayoutCount = 3;

    VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_instancedMeshPipelineLayout));

    PipelineBuilder pipelineBuilder;

    //use the triangle layout we created
    pipelineBuilder._pipelineLayout = _instancedMeshPipelineLayout;
    //connecting the vertex and pixel shaders to the pipeline
    pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
    //it will draw triangles
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    //filled triangles
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    //no backface culling
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    //no multisampling
    pipelineBuilder.set_multisampling_none();
    //pipelineBuilder.set_multisampling(VK_SAMPLE_COUNT_4_BIT);
    //no blending
    pipelineBuilder.disable_blending();
    //pipelineBuilder.enable_blending_additive();

    pipelineBuilder.disable_depthtest();
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    //connect the image format we will draw into, from draw image
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_depthImage.imageFormat);

    //finally build the pipeline
    _instancedMeshPipeline = pipelineBuilder.build_pipeline(_device);

    //clean structures
    vkDestroyShaderModule(_device, triangleFragShader, nullptr);
    vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _instancedMeshPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _instancedMeshPipeline, nullptr);
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
    pipelineBuilder.set_multisampling_none();
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
