#include <VkHelper.h>

#include <random>
#include <glm/gtx/string_cast.hpp>

#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <AssimpModel.h>

#include <CommandPool.h>
#include <CommandBuffer.h>
#include <SyncObjects.h>
#include <FramebufferAttachment.h>
#include <PipelineLayout.h>
#include <SkinningPipeline.h>
#include <ComputePipeline.h>
#include <LinePipeline.h>
#include <GridLinePipeline.h>
#include <GroundMeshPipeline.h>
#include <SkyboxPipeline.h>
#include <CompositePipeline.h>
#include <SSAOPipeline.h>
#include <UniformBuffer.h>
#include <ShaderStorageBuffer.h>
#include <VertexBuffer.h>
#include <Texture.h>

#include <Logger.h>

bool VkHelper::initVulkan(VkRenderData& renderData) {
  if (!deviceInit(renderData)) {
    return false;
  }

  if (!initVma(renderData)) {
    return false;
  }

  if (!getQueues(renderData)) {
    return false;
  }

  if (!createSwapchain(renderData)) {
    return false;
  }

  /* must be done AFTER swapchain as we need data from it */
  if (!createDepthBuffer(renderData)) {
    return false;
  }

  if (!createCommandPools(renderData)) {
    return false;
  }

  if (!createCommandBuffers(renderData)) {
    return false;
  }

  if (!createVertexBuffers(renderData)) {
    return false;
  }

  if (!createUBOs(renderData)) {
    return false;
  }

  if (!createSSBOs(renderData)) {
    return false;
  }

  if (!createDescriptorPool(renderData)) {
    return false;
  }

  if (!createDescriptorLayouts(renderData)) {
    return false;
  }

  if (!createDescriptorSets(renderData)) {
    return false;
  }

  if (!createImages(renderData)) {
    return false;
  }

  if (!createGBuffer(renderData)) {
    return false;
  }

  initSSAO(renderData);

  updateImageDescriptorSets(renderData);

  if (!createPipelineLayouts(renderData)) {
    return false;
  }

  if (!createPipelines(renderData)) {
    return false;
  }

  if (!createSyncObjects(renderData)) {
    return false;
  }

  return true;
}

bool VkHelper::deviceInit(VkRenderData& renderData) {
  /* instance and window - we need at least Vukan 1.1 for the "VK_KHR_maintenance1" extension */
  vkb::InstanceBuilder instBuild;
  auto instRet = instBuild
    .use_default_debug_messenger()
    .request_validation_layers()
    .require_api_version(1, 3, 0)
    .build();

  if (!instRet) {
    Logger::log(1, "%s error: could not build vkb instance\n", __FUNCTION__);
    return false;
  }
  renderData.rdVkbInstance = instRet.value();

  VkResult result = glfwCreateWindowSurface(renderData.rdVkbInstance, renderData.rdWindow, nullptr, &renderData.rdSurface);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: Could not create Vulkan surface (error: %i)\n", __FUNCTION__);
    return false;
  }

  /* force anisotropy */
  VkPhysicalDeviceFeatures requiredFeatures{};
  requiredFeatures.samplerAnisotropy = VK_TRUE;

  /* just get the first available device */
  vkb::PhysicalDeviceSelector physicalDevSel{renderData.rdVkbInstance};
  auto firstPysicalDevSelRet = physicalDevSel
    .set_surface(renderData.rdSurface)
    .set_required_features(requiredFeatures)
    .add_required_extension("VK_KHR_dynamic_rendering")
    .add_required_extension("VK_KHR_dynamic_rendering_local_read")
    .select();

  if (!firstPysicalDevSelRet) {
    Logger::log(1, "%s error: could not get physical devices\n", __FUNCTION__);
    return false;
  }

  /* a 2nd call is required to enable all the supported features, like wideLines */
  VkPhysicalDeviceFeatures physFeatures;
  vkGetPhysicalDeviceFeatures(firstPysicalDevSelRet.value(), &physFeatures);

  auto secondPhysicalDevSelRet = physicalDevSel
    .set_surface(renderData.rdSurface)
    .set_required_features(physFeatures)
    .select();

  if (!secondPhysicalDevSelRet) {
    Logger::log(1, "%s error: could not get physical devices\n", __FUNCTION__);
    return false;
  }

  renderData.rdVkbPhysicalDevice = secondPhysicalDevSelRet.value();
  Logger::log(1, "%s: found physical device '%s'\n", __FUNCTION__, renderData.rdVkbPhysicalDevice.name.c_str());

  /* required for dynamic buffer with world position matrices */
  VkDeviceSize minSSBOOffsetAlignment = renderData.rdVkbPhysicalDevice.properties.limits.minStorageBufferOffsetAlignment;
  Logger::log(1, "%s: the physical device has a minimal SSBO offset of %i bytes\n", __FUNCTION__, minSSBOOffsetAlignment);
  renderData.rdMinSSBOOffsetAlignment = std::max(minSSBOOffsetAlignment, sizeof(glm::mat4));
  Logger::log(1, "%s: SSBO offset has been adjusted to %i bytes\n", __FUNCTION__, renderData.rdMinSSBOOffsetAlignment);

  // enable dynamic rendering
  VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeature {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
    .dynamicRendering = VK_TRUE,
  };

  // Provided by VK_VERSION_1_4
  VkPhysicalDeviceDynamicRenderingLocalReadFeatures dynamicRenderingLocalReadFeature {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES,
    .dynamicRenderingLocalRead = VK_TRUE,
  };

  vkb::DeviceBuilder devBuilder{renderData.rdVkbPhysicalDevice};
  auto devBuilderRet = devBuilder
  .add_pNext(&dynamicRenderingFeature)
  .add_pNext(&dynamicRenderingLocalReadFeature)
  .build();
  if (!devBuilderRet) {
    Logger::log(1, "%s error: could not get devices\n", __FUNCTION__);
    return false;
  }
  renderData.rdVkbDevice = devBuilderRet.value();

  return true;
}

bool VkHelper::initVma(VkRenderData& renderData) {
  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
  allocatorInfo.physicalDevice = renderData.rdVkbPhysicalDevice.physical_device;
  allocatorInfo.device = renderData.rdVkbDevice.device;
  allocatorInfo.instance = renderData.rdVkbInstance.instance;

  VkResult result = vmaCreateAllocator(&allocatorInfo, &renderData.rdAllocator);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not init VMA (error %i)\n", __FUNCTION__, result);
    return false;
  }
  return true;
}

bool VkHelper::getQueues(VkRenderData& renderData) {
  auto graphQueueRet = renderData.rdVkbDevice.get_queue(vkb::QueueType::graphics);
  if (!graphQueueRet.has_value()) {
    Logger::log(1, "%s error: could not get graphics queue\n", __FUNCTION__);
    return false;
  }
  renderData.rdGraphicsQueue = graphQueueRet.value();

  auto presentQueueRet = renderData.rdVkbDevice.get_queue(vkb::QueueType::present);
  if (!presentQueueRet.has_value()) {
    Logger::log(1, "%s error: could not get present queue\n", __FUNCTION__);
    return false;
  }
  renderData.rdPresentQueue = presentQueueRet.value();

  auto computeQueueRet = renderData.rdVkbDevice.get_queue(vkb::QueueType::compute);
  if (!computeQueueRet.has_value()) {
    Logger::log(1, "%s: using shared graphics/compute queue\n", __FUNCTION__);
    renderData.rdComputeQueue = renderData.rdGraphicsQueue;
    renderData.rdHasDedicatedComputeQueue = false;
  } else {
    Logger::log(1, "%s: using separate compute queue\n", __FUNCTION__);
    renderData.rdComputeQueue = computeQueueRet.value();
    renderData.rdHasDedicatedComputeQueue = true;
  }

  return true;
}

bool VkHelper::createSwapchain(VkRenderData& renderData) {
  vkb::SwapchainBuilder swapChainBuild{renderData.rdVkbDevice};
  VkSurfaceFormatKHR surfaceFormat;

  /* set surface to non-sRGB */
  surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  surfaceFormat.format = VK_FORMAT_B8G8R8A8_UNORM;

  /* VK_PRESENT_MODE_FIFO_KHR enables vsync */
  auto  swapChainBuildRet = swapChainBuild
    .set_old_swapchain(renderData.rdVkbSwapchain)
    .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
    .set_desired_format(surfaceFormat)
    .set_desired_min_image_count(renderData.MAX_FRAMES_IN_FLIGHT)
    .build();

  if (!swapChainBuildRet) {
    Logger::log(1, "%s error: could not init swapchain\n", __FUNCTION__);
    return false;
  }

  vkb::destroy_swapchain(renderData.rdVkbSwapchain);
  renderData.rdVkbSwapchain = swapChainBuildRet.value();
  renderData.rdSwapchainImages = swapChainBuildRet.value().get_images().value();
  renderData.rdSwapchainImageViews = swapChainBuildRet.value().get_image_views().value();

  return true;
}

bool VkHelper::recreateSwapchain(VkRenderData& renderData) {
  Logger::log(1, "%s: recreate swapchain\n", __FUNCTION__);

  /* handle minimize */
  glfwGetFramebufferSize(renderData.rdWindow, &renderData.rdWidth, &renderData.rdHeight);
  while (renderData.rdWidth == 0 || renderData.rdHeight == 0) {
    glfwGetFramebufferSize(renderData.rdWindow, &renderData.rdWidth, &renderData.rdHeight);
    glfwWaitEvents();
  }

  vkDeviceWaitIdle(renderData.rdVkbDevice.device);

  /* cleanup */
  cleanupDepthBuffer(renderData);
  cleanupGBuffer(renderData);
  cleanupImages(renderData);

  renderData.rdVkbSwapchain.destroy_image_views(renderData.rdSwapchainImageViews);

  /* and recreate */
  if (!createSwapchain(renderData)) {
    Logger::log(1, "%s error: could not recreate swapchain\n", __FUNCTION__);
    return false;
  }

  if (!createDepthBuffer(renderData)) {
    Logger::log(1, "%s error: could not recreate depth buffer\n", __FUNCTION__);
    return false;
  }

  if (!createImages(renderData)) {
    Logger::log(1, "%s error: could not recreate image buffers\n", __FUNCTION__);
    return false;
  }

  if (!createGBuffer(renderData)) {
    Logger::log(1, "%s error: could not recreate G-Buffer buffer\n", __FUNCTION__);
    return false;
  }

  updateImageDescriptorSets(renderData);

  return true;
}


bool VkHelper::createCommandPools(VkRenderData& renderData) {
  if (!CommandPool::init(renderData, vkb::QueueType::graphics, renderData.rdCommandPool)) {
    Logger::log(1, "%s error: could not create graphics command pool\n", __FUNCTION__);
    return false;
  }

  /* use graphics queue if we have a shared queue  */
  vkb::QueueType computeQueue = renderData.rdHasDedicatedComputeQueue ? vkb::QueueType::compute : vkb::QueueType::graphics;
  if (!CommandPool::init(renderData, computeQueue, renderData.rdComputeCommandPool)) {
    Logger::log(1, "%s error: could not create compute command pool\n", __FUNCTION__);
    return false;
  }

  return true;
}

bool VkHelper::createCommandBuffers(VkRenderData& renderData) {
  renderData.rdCommandBuffers.resize(renderData.MAX_FRAMES_IN_FLIGHT);
  renderData.rdComputeCommandBuffers.resize(renderData.MAX_FRAMES_IN_FLIGHT);

  for (int i = 0; i < renderData.MAX_FRAMES_IN_FLIGHT; ++i) {
    if (!CommandBuffer::init(renderData,renderData.rdCommandPool, renderData.rdCommandBuffers[i])) {
      Logger::log(1, "%s error: could not create command buffers\n", __FUNCTION__);
      return false;
    }

    if (!CommandBuffer::init(renderData,renderData.rdComputeCommandPool, renderData.rdComputeCommandBuffers[i])) {
      Logger::log(1, "%s error: could not create compute command buffers\n", __FUNCTION__);
      return false;
    }
  }

  return true;
}

bool VkHelper::createUBOs(VkRenderData& renderData) {
  if (!UniformBuffer::init(renderData, renderData.rdRenderUploadDataUBO, sizeof(VkRenderUploadData))) {
    Logger::log(1, "%s error: could not create matrix uniform buffers\n", __FUNCTION__);
    return false;
  }
  if (!UniformBuffer::init(renderData, renderData.rdSSAOSettingsUBO, sizeof(VkSSAOUploadData))) {
    Logger::log(1, "%s error: could not create SSAO kernel uniform buffers\n", __FUNCTION__);
    return false;
  }
  if (!UniformBuffer::init(renderData, renderData.rdSSAOKernelSamplesUBO, sizeof(glm::vec3) * 64)) {
    Logger::log(1, "%s error: could not create SSAO kernel uniform buffers\n", __FUNCTION__);
    return false;
  }
  return true;
}

bool VkHelper::createSSBOs(VkRenderData& renderData) {
  if (!ShaderStorageBuffer::init(renderData, renderData.rdShaderTRSMatrixBuffer)) {
    Logger::log(1, "%s error: could not create TRS matrices SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdShaderModelRootMatrixBuffer)) {
    Logger::log(1, "%s error: could not create nodel root position SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdPerInstanceAnimDataBuffer)) {
    Logger::log(1, "%s error: could not create node transform SSBO\n", __FUNCTION__);
    return false;
  }

  /* we must read back data */
  if (!ShaderStorageBuffer::init(renderData, renderData.rdShaderBoneMatrixBuffer)) {
    Logger::log(1, "%s error: could not create bone matrix SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdSelectedInstanceBuffer)) {
    Logger::log(1, "%s error: could not create selection SSBO\n", __FUNCTION__);
    return false;
  }

  /* we must read back data */
  if (!ShaderStorageBuffer::init(renderData, renderData.rdBoundingSphereBuffer)) {
    Logger::log(1, "%s error: could not create bounding sphere SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdSphereModelRootMatrixBuffer)) {
    Logger::log(1, "%s error: could not create nodel root position SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdSpherePerInstanceAnimDataBuffer)) {
    Logger::log(1, "%s error: could not create node transform SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdSphereTRSMatrixBuffer)) {
    Logger::log(1, "%s error: could not create TRS matrices SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdSphereBoneMatrixBuffer)) {
    Logger::log(1, "%s error: could not create bone matrix SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdFaceAnimPerInstanceDataBuffer)) {
    Logger::log(1, "%s error: could not create face anim SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdShaderLevelRootMatrixBuffer)) {
    Logger::log(1, "%s error: could not create level world pos SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdIKBoneMatrixBuffer)) {
    Logger::log(1, "%s error: could not create inverse kinematics matrix SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdIKTRSMatrixBuffer)) {
    Logger::log(1, "%s error: could not create inverse kinematics TRS data SSBO\n", __FUNCTION__);
    return false;
  }

  return true;
}

bool VkHelper::createDescriptorPool(VkRenderData& renderData) {
  std::vector<VkDescriptorPoolSize> poolSizes =
  {
    { VK_DESCRIPTOR_TYPE_SAMPLER, 10000 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10000 },
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 },
  };

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.maxSets = 10000;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();

  VkResult result = vkCreateDescriptorPool(renderData.rdVkbDevice.device, &poolInfo, nullptr, &renderData.rdDescriptorPool);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not init descriptor pool (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  return true;
}

bool VkHelper::createDescriptorLayouts(VkRenderData& renderData) {
  VkResult result;

  {
    /* texture */
    VkDescriptorSetLayoutBinding assimpTextureBind{};
    assimpTextureBind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    assimpTextureBind.binding = 0;
    assimpTextureBind.descriptorCount = 1;
    assimpTextureBind.pImmutableSamplers = nullptr;
    assimpTextureBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpTexBindings = { assimpTextureBind };

    VkDescriptorSetLayoutCreateInfo assimpTextureCreateInfo{};
    assimpTextureCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpTextureCreateInfo.bindingCount = static_cast<uint32_t>(assimpTexBindings.size());
    assimpTextureCreateInfo.pBindings = assimpTexBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpTextureCreateInfo,
      nullptr, &renderData.rdAssimpTextureDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp texture descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* non-animated shader */
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSsboBind{};
    assimpSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSsboBind.binding = 1;
    assimpSsboBind.descriptorCount = 1;
    assimpSsboBind.pImmutableSamplers = nullptr;
    assimpSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSsboBind2{};
    assimpSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSsboBind2.binding = 2;
    assimpSsboBind2.descriptorCount = 1;
    assimpSsboBind2.pImmutableSamplers = nullptr;
    assimpSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { renderDataUboBind, assimpSsboBind, assimpSsboBind2 };

    VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
    assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
    assimpCreateInfo.pBindings = assimpBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpCreateInfo,
      nullptr, &renderData.rdAssimpDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* animated shader */
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind{};
    assimpSkinningSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind.binding = 1;
    assimpSkinningSsboBind.descriptorCount = 1;
    assimpSkinningSsboBind.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind2{};
    assimpSkinningSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind2.binding = 2;
    assimpSkinningSsboBind2.descriptorCount = 1;
    assimpSkinningSsboBind2.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind3{};
    assimpSkinningSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind3.binding = 3;
    assimpSkinningSsboBind3.descriptorCount = 1;
    assimpSkinningSsboBind3.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings =
      { renderDataUboBind, assimpSkinningSsboBind, assimpSkinningSsboBind2, assimpSkinningSsboBind3 };

    VkDescriptorSetLayoutCreateInfo assimpSkinningCreateInfo{};
    assimpSkinningCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpSkinningCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
    assimpSkinningCreateInfo.pBindings = assimpSkinningBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpSkinningCreateInfo,
      nullptr, &renderData.rdAssimpSkinningDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp skinning buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* non-animated selection shader */
    VkDescriptorSetLayoutBinding assimpSelUboBind{};
    assimpSelUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    assimpSelUboBind.binding = 0;
    assimpSelUboBind.descriptorCount = 1;
    assimpSelUboBind.pImmutableSamplers = nullptr;
    assimpSelUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSelSsboBind{};
    assimpSelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSelSsboBind.binding = 1;
    assimpSelSsboBind.descriptorCount = 1;
    assimpSelSsboBind.pImmutableSamplers = nullptr;
    assimpSelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSelSsboBind2{};
    assimpSelSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSelSsboBind2.binding = 2;
    assimpSelSsboBind2.descriptorCount = 1;
    assimpSelSsboBind2.pImmutableSamplers = nullptr;
    assimpSelSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { assimpSelUboBind, assimpSelSsboBind, assimpSelSsboBind2 };

    VkDescriptorSetLayoutCreateInfo assimpSelCreateInfo{};
    assimpSelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpSelCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
    assimpSelCreateInfo.pBindings = assimpBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpSelCreateInfo,
      nullptr, &renderData.rdAssimpSelectionDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp selection buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* animated selection shader */
    VkDescriptorSetLayoutBinding assimpSelUboBind{};
    assimpSelUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    assimpSelUboBind.binding = 0;
    assimpSelUboBind.descriptorCount = 1;
    assimpSelUboBind.pImmutableSamplers = nullptr;
    assimpSelUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind{};
    assimpSkinningSelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind.binding = 1;
    assimpSkinningSelSsboBind.descriptorCount = 1;
    assimpSkinningSelSsboBind.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind2{};
    assimpSkinningSelSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind2.binding = 2;
    assimpSkinningSelSsboBind2.descriptorCount = 1;
    assimpSkinningSelSsboBind2.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind3{};
    assimpSkinningSelSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind3.binding = 3;
    assimpSkinningSelSsboBind3.descriptorCount = 1;
    assimpSkinningSelSsboBind3.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings =
      { assimpSelUboBind, assimpSkinningSelSsboBind, assimpSkinningSelSsboBind2, assimpSkinningSelSsboBind3 };

    VkDescriptorSetLayoutCreateInfo assimpSkinningCreateInfo{};
    assimpSkinningCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpSkinningCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
    assimpSkinningCreateInfo.pBindings = assimpSkinningBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpSkinningCreateInfo,
      nullptr, &renderData.rdAssimpSkinningSelectionDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp skinning selection buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* animated shader with morphs */
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind{};
    assimpSkinningSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind.binding = 1;
    assimpSkinningSsboBind.descriptorCount = 1;
    assimpSkinningSsboBind.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind2{};
    assimpSkinningSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind2.binding = 2;
    assimpSkinningSsboBind2.descriptorCount = 1;
    assimpSkinningSsboBind2.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind3{};
    assimpSkinningSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind3.binding = 3;
    assimpSkinningSsboBind3.descriptorCount = 1;
    assimpSkinningSsboBind3.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind4{};
    assimpSkinningSsboBind4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind4.binding = 4;
    assimpSkinningSsboBind4.descriptorCount = 1;
    assimpSkinningSsboBind4.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind4.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings =
      { renderDataUboBind, assimpSkinningSsboBind,
        assimpSkinningSsboBind2, assimpSkinningSsboBind3, assimpSkinningSsboBind4 };

    VkDescriptorSetLayoutCreateInfo assimpMorphSkinningCreateInfo{};
    assimpMorphSkinningCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMorphSkinningCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
    assimpMorphSkinningCreateInfo.pBindings = assimpSkinningBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMorphSkinningCreateInfo,
      nullptr, &renderData.rdAssimpSkinningMorphDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp morph skinning buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* animated plus morphs selection shader */
    VkDescriptorSetLayoutBinding assimpSelUboBind{};
    assimpSelUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    assimpSelUboBind.binding = 0;
    assimpSelUboBind.descriptorCount = 1;
    assimpSelUboBind.pImmutableSamplers = nullptr;
    assimpSelUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind{};
    assimpSkinningSelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind.binding = 1;
    assimpSkinningSelSsboBind.descriptorCount = 1;
    assimpSkinningSelSsboBind.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind2{};
    assimpSkinningSelSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind2.binding = 2;
    assimpSkinningSelSsboBind2.descriptorCount = 1;
    assimpSkinningSelSsboBind2.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind3{};
    assimpSkinningSelSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind3.binding = 3;
    assimpSkinningSelSsboBind3.descriptorCount = 1;
    assimpSkinningSelSsboBind3.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind4{};
    assimpSkinningSelSsboBind4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind4.binding = 4;
    assimpSkinningSelSsboBind4.descriptorCount = 1;
    assimpSkinningSelSsboBind4.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind4.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings =
    { assimpSelUboBind, assimpSkinningSelSsboBind,
      assimpSkinningSelSsboBind2, assimpSkinningSelSsboBind3, assimpSkinningSelSsboBind4 };

    VkDescriptorSetLayoutCreateInfo assimpMorphSkinningSelectionCreateInfo{};
    assimpMorphSkinningSelectionCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMorphSkinningSelectionCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
    assimpMorphSkinningSelectionCreateInfo.pBindings = assimpSkinningBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMorphSkinningSelectionCreateInfo,
      nullptr, &renderData.rdAssimpSkinningMorphSelectionDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp morph skinning selection buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* animated plus morphs, per-model */
    VkDescriptorSetLayoutBinding assimpMorphPerModelSsboBind{};
    assimpMorphPerModelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpMorphPerModelSsboBind.binding = 0;
    assimpMorphPerModelSsboBind.descriptorCount = 1;
    assimpMorphPerModelSsboBind.pImmutableSamplers = nullptr;
    assimpMorphPerModelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo assimpMorphPerModelCreateInfo{};
    assimpMorphPerModelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMorphPerModelCreateInfo.bindingCount = 1;
    assimpMorphPerModelCreateInfo.pBindings = &assimpMorphPerModelSsboBind;

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMorphPerModelCreateInfo,
      nullptr, &renderData.rdAssimpSkinningMorphPerModelDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp morph skinning selection per-model buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* assimp level */
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSsboBind{};
    assimpSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSsboBind.binding = 1;
    assimpSsboBind.descriptorCount = 1;
    assimpSsboBind.pImmutableSamplers = nullptr;
    assimpSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { renderDataUboBind, assimpSsboBind };

    VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
    assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
    assimpCreateInfo.pBindings = assimpBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpCreateInfo,
      nullptr, &renderData.rdAssimpLevelDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp Level buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* ground mesh */
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { renderDataUboBind };

    VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
    assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
    assimpCreateInfo.pBindings = assimpBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpCreateInfo,
      nullptr, &renderData.rdGroundMeshDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp ground mesh buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* compute transformation shader, global  */
    VkDescriptorSetLayoutBinding assimpTransformSsboBind{};
    assimpTransformSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpTransformSsboBind.binding = 0;
    assimpTransformSsboBind.descriptorCount = 1;
    assimpTransformSsboBind.pImmutableSamplers = nullptr;
    assimpTransformSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding assimpTrsSsboBind{};
    assimpTrsSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpTrsSsboBind.binding = 1;
    assimpTrsSsboBind.descriptorCount = 1;
    assimpTrsSsboBind.pImmutableSamplers = nullptr;
    assimpTrsSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpTransformBindings = { assimpTransformSsboBind, assimpTrsSsboBind };

    VkDescriptorSetLayoutCreateInfo assimpTransformCreateInfo{};
    assimpTransformCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpTransformCreateInfo.bindingCount = static_cast<uint32_t>(assimpTransformBindings.size());
    assimpTransformCreateInfo.pBindings = assimpTransformBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpTransformCreateInfo,
      nullptr, &renderData.rdAssimpComputeTransformDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp transform global compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* compute transformation shader, per-model  */
    VkDescriptorSetLayoutBinding assimpAnimLookupSsboBind{};
    assimpAnimLookupSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpAnimLookupSsboBind.binding = 0;
    assimpAnimLookupSsboBind.descriptorCount = 1;
    assimpAnimLookupSsboBind.pImmutableSamplers = nullptr;
    assimpAnimLookupSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo assimpTransformPerModelCreateInfo{};
    assimpTransformPerModelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpTransformPerModelCreateInfo.bindingCount = 1;
    assimpTransformPerModelCreateInfo.pBindings = &assimpAnimLookupSsboBind;

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpTransformPerModelCreateInfo,
      nullptr, &renderData.rdAssimpComputeTransformPerModelDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp transform per model compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* compute matrix multiplication shader, global data */
    VkDescriptorSetLayoutBinding assimpTrsSsboBind{};
    assimpTrsSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpTrsSsboBind.binding = 0;
    assimpTrsSsboBind.descriptorCount = 1;
    assimpTrsSsboBind.pImmutableSamplers = nullptr;
    assimpTrsSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding assimpNodeMatricesSsboBind{};
    assimpNodeMatricesSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpNodeMatricesSsboBind.binding = 1;
    assimpNodeMatricesSsboBind.descriptorCount = 1;
    assimpNodeMatricesSsboBind.pImmutableSamplers = nullptr;
    assimpNodeMatricesSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpMatMultBindings =
      { assimpTrsSsboBind,assimpNodeMatricesSsboBind };

    VkDescriptorSetLayoutCreateInfo assimpMatrixMultCreateInfo{};
    assimpMatrixMultCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMatrixMultCreateInfo.bindingCount = static_cast<uint32_t>(assimpMatMultBindings.size());
    assimpMatrixMultCreateInfo.pBindings = assimpMatMultBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMatrixMultCreateInfo,
      nullptr, &renderData.rdAssimpComputeMatrixMultDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp matrix multiplication global compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* compute matrix multiplication shader, per-model data */
    VkDescriptorSetLayoutBinding assimpParentMatrixSsboBind{};
    assimpParentMatrixSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpParentMatrixSsboBind.binding = 0;
    assimpParentMatrixSsboBind.descriptorCount = 1;
    assimpParentMatrixSsboBind.pImmutableSamplers = nullptr;
    assimpParentMatrixSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding assimpBoneOffsetSsboBind{};
    assimpBoneOffsetSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpBoneOffsetSsboBind.binding = 1;
    assimpBoneOffsetSsboBind.descriptorCount = 1;
    assimpBoneOffsetSsboBind.pImmutableSamplers = nullptr;
    assimpBoneOffsetSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding assimpBoundingSpheresSsboBind{};
    assimpBoundingSpheresSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpBoundingSpheresSsboBind.binding = 2;
    assimpBoundingSpheresSsboBind.descriptorCount = 1;
    assimpBoundingSpheresSsboBind.pImmutableSamplers = nullptr;
    assimpBoundingSpheresSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBoundSpheresPerModelBindings =
      { assimpParentMatrixSsboBind, assimpBoneOffsetSsboBind, assimpBoundingSpheresSsboBind };

    VkDescriptorSetLayoutCreateInfo assimpMatrixMultPerModelCreateInfo{};
    assimpMatrixMultPerModelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMatrixMultPerModelCreateInfo.bindingCount = static_cast<uint32_t>(assimpBoundSpheresPerModelBindings.size());
    assimpMatrixMultPerModelCreateInfo.pBindings = assimpBoundSpheresPerModelBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMatrixMultPerModelCreateInfo,
      nullptr, &renderData.rdAssimpComputeMatrixMultPerModelDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp bounding sphere per model compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* compute bounding spheres shader, global data */
    VkDescriptorSetLayoutBinding assimpNodeMatrixSsboBind{};
    assimpNodeMatrixSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpNodeMatrixSsboBind.binding = 0;
    assimpNodeMatrixSsboBind.descriptorCount = 1;
    assimpNodeMatrixSsboBind.pImmutableSamplers = nullptr;
    assimpNodeMatrixSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding assimpWorldPosMatricesSsboBind{};
    assimpWorldPosMatricesSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpWorldPosMatricesSsboBind.binding = 1;
    assimpWorldPosMatricesSsboBind.descriptorCount = 1;
    assimpWorldPosMatricesSsboBind.pImmutableSamplers = nullptr;
    assimpWorldPosMatricesSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding boundingSpheresSsboBind{};
    boundingSpheresSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boundingSpheresSsboBind.binding = 2;
    boundingSpheresSsboBind.descriptorCount = 1;
    boundingSpheresSsboBind.pImmutableSamplers = nullptr;
    boundingSpheresSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpMatMultBindings =
    { assimpNodeMatrixSsboBind, assimpWorldPosMatricesSsboBind, boundingSpheresSsboBind };

    VkDescriptorSetLayoutCreateInfo assimpMatrixMultCreateInfo{};
    assimpMatrixMultCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMatrixMultCreateInfo.bindingCount = static_cast<uint32_t>(assimpMatMultBindings.size());
    assimpMatrixMultCreateInfo.pBindings = assimpMatMultBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMatrixMultCreateInfo,
      nullptr, &renderData.rdAssimpComputeBoundingSpheresDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp bounding spheres global compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* compute bounding spheres shader, per-model data */
    VkDescriptorSetLayoutBinding assimpParentMatrixSsboBind{};
    assimpParentMatrixSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpParentMatrixSsboBind.binding = 0;
    assimpParentMatrixSsboBind.descriptorCount = 1;
    assimpParentMatrixSsboBind.pImmutableSamplers = nullptr;
    assimpParentMatrixSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding boundingSphereAdjustmentsSsboBind{};
    boundingSphereAdjustmentsSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boundingSphereAdjustmentsSsboBind.binding = 1;
    boundingSphereAdjustmentsSsboBind.descriptorCount = 1;
    boundingSphereAdjustmentsSsboBind.pImmutableSamplers = nullptr;
    boundingSphereAdjustmentsSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpMatMultPerModelBindings =
      { assimpParentMatrixSsboBind, boundingSphereAdjustmentsSsboBind};

    VkDescriptorSetLayoutCreateInfo assimpMatrixMultPerModelCreateInfo{};
    assimpMatrixMultPerModelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMatrixMultPerModelCreateInfo.bindingCount = static_cast<uint32_t>(assimpMatMultPerModelBindings.size());
    assimpMatrixMultPerModelCreateInfo.pBindings = assimpMatMultPerModelBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMatrixMultPerModelCreateInfo,
      nullptr, &renderData.rdAssimpComputeBoundingSpheresPerModelDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp bounding spheres per model compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* line shader */
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { renderDataUboBind };

    VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
    assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
    assimpCreateInfo.pBindings = assimpBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpCreateInfo,
      nullptr, &renderData.rdLineDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp line drawing descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* sphere shader */
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpBoundingSpheresSsboBind{};
    assimpBoundingSpheresSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpBoundingSpheresSsboBind.binding = 1;
    assimpBoundingSpheresSsboBind.descriptorCount = 1;
    assimpBoundingSpheresSsboBind.pImmutableSamplers = nullptr;
    assimpBoundingSpheresSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpSphereBindings =
      { renderDataUboBind, assimpBoundingSpheresSsboBind };

    VkDescriptorSetLayoutCreateInfo assimpBoundingSpheresCreateInfo{};
    assimpBoundingSpheresCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpBoundingSpheresCreateInfo.bindingCount = static_cast<uint32_t>(assimpSphereBindings.size());
    assimpBoundingSpheresCreateInfo.pBindings = assimpSphereBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpBoundingSpheresCreateInfo,
      nullptr, &renderData.rdSphereDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp bounding sphere drawing descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* skybox shader */
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo assimpSkyboxCreateInfo{};
    assimpSkyboxCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpSkyboxCreateInfo.bindingCount = 1;
    assimpSkyboxCreateInfo.pBindings = &renderDataUboBind;

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpSkyboxCreateInfo,
      nullptr, &renderData.rdSkyboxDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp skybox descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* Composite shader */
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding colorBinding{};
    colorBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    colorBinding.binding = 1;
    colorBinding.descriptorCount = 1;
    colorBinding.pImmutableSamplers = nullptr;
    colorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding positionBinding{};
    positionBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    positionBinding.binding = 2;
    positionBinding.descriptorCount = 1;
    positionBinding.pImmutableSamplers = nullptr;
    positionBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    normalBinding.binding = 3;
    normalBinding.descriptorCount = 1;
    normalBinding.pImmutableSamplers = nullptr;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding selectionBinding{};
    selectionBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    selectionBinding.binding = 4;
    selectionBinding.descriptorCount = 1;
    selectionBinding.pImmutableSamplers = nullptr;
    selectionBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoColorBinding{};
    ssaoColorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoColorBinding.binding = 5;
    ssaoColorBinding.descriptorCount = 1;
    ssaoColorBinding.pImmutableSamplers = nullptr;
    ssaoColorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoBlurBinding{};
    ssaoBlurBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoBlurBinding.binding = 6;
    ssaoBlurBinding.descriptorCount = 1;
    ssaoBlurBinding.pImmutableSamplers = nullptr;
    ssaoBlurBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {
      renderDataUboBind,
      colorBinding,
      positionBinding,
      normalBinding,
      selectionBinding,
      ssaoColorBinding,
      ssaoBlurBinding
    };

    VkDescriptorSetLayoutCreateInfo compositeLayoutInfo{};
    compositeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    compositeLayoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    compositeLayoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &compositeLayoutInfo,
        nullptr, &renderData.rdCompositeDescriptorLayout) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create descriptor set layout\n", __FUNCTION__);
      return false;
    }
  }

  {
    /* SSAO shader */
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoSettingsUboBind{};
    ssaoSettingsUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ssaoSettingsUboBind.binding = 1;
    ssaoSettingsUboBind.descriptorCount = 1;
    ssaoSettingsUboBind.pImmutableSamplers = nullptr;
    ssaoSettingsUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthImageBinding{};
    depthImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageBinding.binding = 2;
    depthImageBinding.descriptorCount = 1;
    depthImageBinding.pImmutableSamplers = nullptr;
    depthImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalBinding.binding = 3;
    normalBinding.descriptorCount = 1;
    normalBinding.pImmutableSamplers = nullptr;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoNoiseTextureBind{};
    ssaoNoiseTextureBind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoNoiseTextureBind.binding = 4;
    ssaoNoiseTextureBind.descriptorCount = 1;
    ssaoNoiseTextureBind.pImmutableSamplers = nullptr;
    ssaoNoiseTextureBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoUboBind{};
    ssaoUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ssaoUboBind.binding = 5;
    ssaoUboBind.descriptorCount = 1;
    ssaoUboBind.pImmutableSamplers = nullptr;
    ssaoUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {
      renderDataUboBind,
      ssaoSettingsUboBind,
      depthImageBinding,
      normalBinding,
      ssaoNoiseTextureBind,
      ssaoUboBind
    };

    VkDescriptorSetLayoutCreateInfo ssaoLayoutInfo{};
    ssaoLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ssaoLayoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    ssaoLayoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &ssaoLayoutInfo,
      nullptr, &renderData.rdSSAODescriptorLayout) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create ssao descriptor set layout\n", __FUNCTION__);
    return false;
      }
  }

  {
    /* SSAO blur shader */
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutBinding ssaoSettingsUboBind{};

    ssaoSettingsUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ssaoSettingsUboBind.binding = 1;
    ssaoSettingsUboBind.descriptorCount = 1;
    ssaoSettingsUboBind.pImmutableSamplers = nullptr;
    ssaoSettingsUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthImageBinding{};
    depthImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageBinding.binding = 2;
    depthImageBinding.descriptorCount = 1;
    depthImageBinding.pImmutableSamplers = nullptr;
    depthImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalBinding.binding = 3;
    normalBinding.descriptorCount = 1;
    normalBinding.pImmutableSamplers = nullptr;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoColorBinding{};
    ssaoColorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoColorBinding.binding = 4;
    ssaoColorBinding.descriptorCount = 1;
    ssaoColorBinding.pImmutableSamplers = nullptr;
    ssaoColorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {
      renderDataUboBind,
      ssaoSettingsUboBind,
      depthImageBinding,
      normalBinding,
      ssaoColorBinding
    };

    VkDescriptorSetLayoutCreateInfo ssaoBlurLayoutInfo{};
    ssaoBlurLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ssaoBlurLayoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    ssaoBlurLayoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &ssaoBlurLayoutInfo,
      nullptr, &renderData.rdSSAOBlurDescriptorLayout) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create ssao descriptor set layout\n", __FUNCTION__);
    return false;
      }
  }

  return true;
}

bool VkHelper::createDescriptorSets(VkRenderData& renderData) {
  {
    /* non-animated models */
    VkDescriptorSetAllocateInfo descriptorAllocateInfo{};
    descriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    descriptorAllocateInfo.descriptorSetCount = 1;
    descriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &descriptorAllocateInfo,
        &renderData.rdAssimpDescriptorSet);
     if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* animated models */
    VkDescriptorSetAllocateInfo skinningDescriptorAllocateInfo{};
    skinningDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skinningDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    skinningDescriptorAllocateInfo.descriptorSetCount = 1;
    skinningDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpSkinningDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &skinningDescriptorAllocateInfo,
      &renderData.rdAssimpSkinningDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Skinning descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* selection, non-animated models */
    VkDescriptorSetAllocateInfo selectionDescriptorAllocateInfo{};
    selectionDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    selectionDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    selectionDescriptorAllocateInfo.descriptorSetCount = 1;
    selectionDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpSelectionDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &selectionDescriptorAllocateInfo,
      &renderData.rdAssimpSelectionDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp selection descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* selection, animated models */
    VkDescriptorSetAllocateInfo skinningSelectionDescriptorAllocateInfo{};
    skinningSelectionDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skinningSelectionDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    skinningSelectionDescriptorAllocateInfo.descriptorSetCount = 1;
    skinningSelectionDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpSkinningSelectionDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &skinningSelectionDescriptorAllocateInfo,
      &renderData.rdAssimpSkinningSelectionDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp skinning selection descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* animated and morphed models */
    VkDescriptorSetAllocateInfo skinningMorphDescriptorAllocateInfo{};
    skinningMorphDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skinningMorphDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    skinningMorphDescriptorAllocateInfo.descriptorSetCount = 1;
    skinningMorphDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpSkinningMorphDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &skinningMorphDescriptorAllocateInfo,
      &renderData.rdAssimpSkinningMorphDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp morph skinning descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* selection, animated and morphed models */
    VkDescriptorSetAllocateInfo skinningMorphSelectionDescriptorAllocateInfo{};
    skinningMorphSelectionDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skinningMorphSelectionDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    skinningMorphSelectionDescriptorAllocateInfo.descriptorSetCount = 1;
    skinningMorphSelectionDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpSkinningMorphSelectionDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &skinningMorphSelectionDescriptorAllocateInfo,
      &renderData.rdAssimpSkinningMorphSelectionDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp morph skinning selection descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* level */
    VkDescriptorSetAllocateInfo levelDescriptorAllocateInfo{};
    levelDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    levelDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    levelDescriptorAllocateInfo.descriptorSetCount = 1;
    levelDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpLevelDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &levelDescriptorAllocateInfo,
      &renderData.rdAssimpLevelDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Level descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* ground-mesh drawing */
    VkDescriptorSetAllocateInfo lineAllocateInfo{};
    lineAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    lineAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    lineAllocateInfo.descriptorSetCount = 1;
    lineAllocateInfo.pSetLayouts = &renderData.rdGroundMeshDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &lineAllocateInfo,
      &renderData.rdGroundMeshDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp ground-mesh drawing descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* compute transform. global data */
    VkDescriptorSetAllocateInfo computeTransformDescriptorAllocateInfo{};
    computeTransformDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeTransformDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeTransformDescriptorAllocateInfo.descriptorSetCount = 1;
    computeTransformDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpComputeTransformDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeTransformDescriptorAllocateInfo,
      &renderData.rdAssimpComputeTransformDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Transform Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* compute transform fpr bounding spheres. global data */
    VkDescriptorSetAllocateInfo computeTransformDescriptorAllocateInfo{};
    computeTransformDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeTransformDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeTransformDescriptorAllocateInfo.descriptorSetCount = 1;
    computeTransformDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpComputeTransformDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeTransformDescriptorAllocateInfo,
      &renderData.rdAssimpComputeSphereTransformDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Bounding Sphere Transform Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* matrix multiplication, global data */
    VkDescriptorSetAllocateInfo computeMatrixMultDescriptorAllocateInfo{};
    computeMatrixMultDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeMatrixMultDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeMatrixMultDescriptorAllocateInfo.descriptorSetCount = 1;
    computeMatrixMultDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpComputeMatrixMultDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeMatrixMultDescriptorAllocateInfo,
      &renderData.rdAssimpComputeMatrixMultDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Matrix Mult Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* matrix multiplication bounding spheres, global data */
    VkDescriptorSetAllocateInfo computeMatrixMultDescriptorAllocateInfo{};
    computeMatrixMultDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeMatrixMultDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeMatrixMultDescriptorAllocateInfo.descriptorSetCount = 1;
    computeMatrixMultDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpComputeMatrixMultDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeMatrixMultDescriptorAllocateInfo,
      &renderData.rdAssimpComputeSphereMatrixMultDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Bounding Sphere Matrix Mult Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* matrix multiplication inverse kinematics, global data */
    VkDescriptorSetAllocateInfo computeMatrixMultIKDescriptorAllocateInfo{};
    computeMatrixMultIKDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeMatrixMultIKDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeMatrixMultIKDescriptorAllocateInfo.descriptorSetCount = 1;
    computeMatrixMultIKDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpComputeMatrixMultDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeMatrixMultIKDescriptorAllocateInfo,
      &renderData.rdAssimpComputeIKDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Inverse Kinematics Matrix Mult Compute descriptor set (error: %i)\n",
        __FUNCTION__, result);
      return false;
    }
  }

  {
    /* bounding spheres, global data */
    VkDescriptorSetAllocateInfo computeBoundingSpheresDescriptorAllocateInfo{};
    computeBoundingSpheresDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeBoundingSpheresDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeBoundingSpheresDescriptorAllocateInfo.descriptorSetCount = 1;
    computeBoundingSpheresDescriptorAllocateInfo.pSetLayouts = &renderData.rdAssimpComputeBoundingSpheresDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeBoundingSpheresDescriptorAllocateInfo,
      &renderData.rdAssimpComputeBoundingSpheresDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Bounding Sphere Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* line-drawing */
    VkDescriptorSetAllocateInfo lineAllocateInfo{};
    lineAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    lineAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    lineAllocateInfo.descriptorSetCount = 1;
    lineAllocateInfo.pSetLayouts = &renderData.rdLineDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &lineAllocateInfo,
      &renderData.rdLineDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp line-drawing descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* sphere-drawing */
    VkDescriptorSetAllocateInfo sphereAllocateInfo{};
    sphereAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sphereAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    sphereAllocateInfo.descriptorSetCount = 1;
    sphereAllocateInfo.pSetLayouts = &renderData.rdSphereDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &sphereAllocateInfo,
      &renderData.rdSphereDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp bounding sphere-drawing descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    /* skybox */
    VkDescriptorSetAllocateInfo skyboxAllocateInfo{};
    skyboxAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skyboxAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    skyboxAllocateInfo.descriptorSetCount = 1;
    skyboxAllocateInfo.pSetLayouts = &renderData.rdSkyboxDescriptorLayout;

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &skyboxAllocateInfo,
      &renderData.rdSkyboxDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp skybox descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  /* composite */
  VkDescriptorSetAllocateInfo compositeAllocInfo{};
  compositeAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  compositeAllocInfo.descriptorPool = renderData.rdDescriptorPool;
  compositeAllocInfo.descriptorSetCount = 1;
  compositeAllocInfo.pSetLayouts = &renderData.rdCompositeDescriptorLayout;

  if (vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &compositeAllocInfo,
      &renderData.rdCompositeDescriptorSet) != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate memory for composite descriptor set", __FUNCTION__);
    return false;
  }

  /* SSAO */
  VkDescriptorSetAllocateInfo ssaoAllocInfo{};
  ssaoAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  ssaoAllocInfo.descriptorPool = renderData.rdDescriptorPool;
  ssaoAllocInfo.descriptorSetCount = 1;
  ssaoAllocInfo.pSetLayouts = &renderData.rdSSAODescriptorLayout;

  if (vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &ssaoAllocInfo,
      &renderData.rdSSAODescriptorSet) != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate memory for ssao descriptor set", __FUNCTION__);
    return false;
  }

  /* SSAO blur */
  VkDescriptorSetAllocateInfo ssaoBlurAllocInfo{};
  ssaoBlurAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  ssaoBlurAllocInfo.descriptorPool = renderData.rdDescriptorPool;
  ssaoBlurAllocInfo.descriptorSetCount = 1;
  ssaoBlurAllocInfo.pSetLayouts = &renderData.rdSSAOBlurDescriptorLayout;

  if (vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &ssaoBlurAllocInfo,
      &renderData.rdSSAOBlurDescriptorSet) != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate memory for ssao blur descriptor set", __FUNCTION__);
    return false;
  }

  updateDescriptorSets(renderData);
  updateComputeDescriptorSets(renderData);
  updateLevelDescriptorSets(renderData);
  updateSphereComputeDescriptorSets(renderData);
  updateIKComputeDescriptorSets(renderData);

  return true;
}

void VkHelper::updateDescriptorSets(VkRenderData& renderData) {
  Logger::log(1, "%s: updating descriptor sets\n", __FUNCTION__);
  /* we must update the descriptor sets whenever the buffer size has changed */
  {
    /* non-animated shader */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffer.buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffer.buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpDescriptorSet;
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpDescriptorSet;
    posWriteDescriptorSet.dstBinding = 1;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpDescriptorSet;
    selectionWriteDescriptorSet.dstBinding = 2;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets =
       { matrixWriteDescriptorSet, posWriteDescriptorSet, selectionWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
       writeDescriptorSets.data(), 0, nullptr);
  }

  {
    /* animated shader */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdShaderBoneMatrixBuffer.buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffer.buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffer.buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningDescriptorSet;
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningDescriptorSet;
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningDescriptorSet;
    posWriteDescriptorSet.dstBinding = 2;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningDescriptorSet;
    selectionWriteDescriptorSet.dstBinding = 3;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    std::vector<VkWriteDescriptorSet> skinningWriteDescriptorSets =
      { matrixWriteDescriptorSet, boneMatrixWriteDescriptorSet, posWriteDescriptorSet, selectionWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(skinningWriteDescriptorSets.size()),
       skinningWriteDescriptorSets.data(), 0, nullptr);
  }

  {
    /* selection shader, non-animated  */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffer.buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffer.buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpSelectionDescriptorSet;
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpSelectionDescriptorSet;
    posWriteDescriptorSet.dstBinding = 1;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpSelectionDescriptorSet;
    selectionWriteDescriptorSet.dstBinding = 2;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    std::vector<VkWriteDescriptorSet> selectionWriteDescriptorSets =
      { matrixWriteDescriptorSet, posWriteDescriptorSet, selectionWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(selectionWriteDescriptorSets.size()),
       selectionWriteDescriptorSets.data(), 0, nullptr);
  }

  {
    /* selection shader, animated  */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdShaderBoneMatrixBuffer.buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffer.buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffer.buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningSelectionDescriptorSet;
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningSelectionDescriptorSet;
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningSelectionDescriptorSet;
    posWriteDescriptorSet.dstBinding = 2;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningSelectionDescriptorSet;
    selectionWriteDescriptorSet.dstBinding = 3;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    std::vector<VkWriteDescriptorSet> skinningSelectionWriteDescriptorSets =
      { matrixWriteDescriptorSet, boneMatrixWriteDescriptorSet,
        posWriteDescriptorSet, selectionWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(skinningSelectionWriteDescriptorSets.size()),
       skinningSelectionWriteDescriptorSets.data(), 0, nullptr);
  }

  {
    /* animated plus morph shader */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdShaderBoneMatrixBuffer.buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffer.buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffer.buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo faceAnimInfo{};
    faceAnimInfo.buffer = renderData.rdFaceAnimPerInstanceDataBuffer.buffer;
    faceAnimInfo.offset = 0;
    faceAnimInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphDescriptorSet;
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphDescriptorSet;
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphDescriptorSet;
    posWriteDescriptorSet.dstBinding = 2;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphDescriptorSet;
    selectionWriteDescriptorSet.dstBinding = 3;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    VkWriteDescriptorSet faceAnimWriteDescriptorSet{};
    faceAnimWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    faceAnimWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    faceAnimWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphDescriptorSet;
    faceAnimWriteDescriptorSet.dstBinding = 4;
    faceAnimWriteDescriptorSet.descriptorCount = 1;
    faceAnimWriteDescriptorSet.pBufferInfo = &faceAnimInfo;

    std::vector<VkWriteDescriptorSet> skinningWriteDescriptorSets =
      { matrixWriteDescriptorSet, boneMatrixWriteDescriptorSet,
        posWriteDescriptorSet, selectionWriteDescriptorSet, faceAnimWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(skinningWriteDescriptorSets.size()),
       skinningWriteDescriptorSets.data(), 0, nullptr);
  }

  {
    /* selection shader, animated plus morph */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdShaderBoneMatrixBuffer.buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffer.buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffer.buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo faceAnimInfo{};
    faceAnimInfo.buffer = renderData.rdFaceAnimPerInstanceDataBuffer.buffer;
    faceAnimInfo.offset = 0;
    faceAnimInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphSelectionDescriptorSet;
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphSelectionDescriptorSet;
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphSelectionDescriptorSet;
    posWriteDescriptorSet.dstBinding = 2;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphSelectionDescriptorSet;
    selectionWriteDescriptorSet.dstBinding = 3;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    VkWriteDescriptorSet faceAnimWriteDescriptorSet{};
    faceAnimWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    faceAnimWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    faceAnimWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphSelectionDescriptorSet;
    faceAnimWriteDescriptorSet.dstBinding = 4;
    faceAnimWriteDescriptorSet.descriptorCount = 1;
    faceAnimWriteDescriptorSet.pBufferInfo = &faceAnimInfo;

    std::vector<VkWriteDescriptorSet> skinningSelectionWriteDescriptorSets =
    { matrixWriteDescriptorSet, boneMatrixWriteDescriptorSet,
      posWriteDescriptorSet, selectionWriteDescriptorSet, faceAnimWriteDescriptorSet };

      vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(skinningSelectionWriteDescriptorSets.size()),
        skinningSelectionWriteDescriptorSets.data(), 0, nullptr);
  }

  {
    /* line-drawing shader */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdLineDescriptorSet;
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets =
    { matrixWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }

  {
    /* ground-mesh-drawing shader */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdGroundMeshDescriptorSet;
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets =
    { matrixWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }

  {
    /* skybox shader */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdSkyboxDescriptorSet;
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets =
      { matrixWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }
}

void VkHelper::updateComputeDescriptorSets(VkRenderData& renderData) {
  Logger::log(1, "%s: updating compute descriptor sets\n", __FUNCTION__);
  {
    /* transform compute shader */
    VkDescriptorBufferInfo transformInfo{};
    transformInfo.buffer = renderData.rdPerInstanceAnimDataBuffer.buffer;
    transformInfo.offset = 0;
    transformInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo trsInfo{};
    trsInfo.buffer = renderData.rdShaderTRSMatrixBuffer.buffer;
    trsInfo.offset = 0;
    trsInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet transformWriteDescriptorSet{};
    transformWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    transformWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    transformWriteDescriptorSet.dstSet = renderData.rdAssimpComputeTransformDescriptorSet;
    transformWriteDescriptorSet.dstBinding = 0;
    transformWriteDescriptorSet.descriptorCount = 1;
    transformWriteDescriptorSet.pBufferInfo = &transformInfo;

    VkWriteDescriptorSet trsWriteDescriptorSet{};
    trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    trsWriteDescriptorSet.dstSet = renderData.rdAssimpComputeTransformDescriptorSet;
    trsWriteDescriptorSet.dstBinding = 1;
    trsWriteDescriptorSet.descriptorCount = 1;
    trsWriteDescriptorSet.pBufferInfo = &trsInfo;

    std::vector<VkWriteDescriptorSet> transformWriteDescriptorSets =
      { transformWriteDescriptorSet, trsWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(transformWriteDescriptorSets.size()),
       transformWriteDescriptorSets.data(), 0, nullptr);
  }

  {
    /* matrix multiplication compute shader, global data */
    VkDescriptorBufferInfo trsInfo{};
    trsInfo.buffer = renderData.rdShaderTRSMatrixBuffer.buffer;
    trsInfo.offset = 0;
    trsInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdShaderBoneMatrixBuffer.buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet trsWriteDescriptorSet{};
    trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    trsWriteDescriptorSet.dstSet = renderData.rdAssimpComputeMatrixMultDescriptorSet;
    trsWriteDescriptorSet.dstBinding = 0;
    trsWriteDescriptorSet.descriptorCount = 1;
    trsWriteDescriptorSet.pBufferInfo = &trsInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpComputeMatrixMultDescriptorSet;
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    std::vector<VkWriteDescriptorSet> matrixMultWriteDescriptorSets =
      { trsWriteDescriptorSet, boneMatrixWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(matrixMultWriteDescriptorSets.size()),
       matrixMultWriteDescriptorSets.data(), 0, nullptr);
  }
}

void VkHelper::updateLevelDescriptorSets(VkRenderData& renderData) {
  Logger::log(1, "%s: updating level descriptor sets\n", __FUNCTION__);
  {
    /* level shader */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderLevelRootMatrixBuffer.buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpLevelDescriptorSet;
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpLevelDescriptorSet;
    posWriteDescriptorSet.dstBinding = 1;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets =
    { matrixWriteDescriptorSet, posWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }
}

void VkHelper::updateSphereComputeDescriptorSets(VkRenderData& renderData) {
  Logger::log(1, "%s: updating sphere descriptor sets\n", __FUNCTION__);
  {
    /* transform compute shader for bounding spheres */
    VkDescriptorBufferInfo transformInfo{};
    transformInfo.buffer = renderData.rdSpherePerInstanceAnimDataBuffer.buffer;
    transformInfo.offset = 0;
    transformInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo trsInfo{};
    trsInfo.buffer = renderData.rdSphereTRSMatrixBuffer.buffer;
    trsInfo.offset = 0;
    trsInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet transformWriteDescriptorSet{};
    transformWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    transformWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    transformWriteDescriptorSet.dstSet = renderData.rdAssimpComputeSphereTransformDescriptorSet;
    transformWriteDescriptorSet.dstBinding = 0;
    transformWriteDescriptorSet.descriptorCount = 1;
    transformWriteDescriptorSet.pBufferInfo = &transformInfo;

    VkWriteDescriptorSet trsWriteDescriptorSet{};
    trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    trsWriteDescriptorSet.dstSet = renderData.rdAssimpComputeSphereTransformDescriptorSet;
    trsWriteDescriptorSet.dstBinding = 1;
    trsWriteDescriptorSet.descriptorCount = 1;
    trsWriteDescriptorSet.pBufferInfo = &trsInfo;

    std::vector<VkWriteDescriptorSet> transformWriteDescriptorSets =
      { transformWriteDescriptorSet, trsWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(transformWriteDescriptorSets.size()),
       transformWriteDescriptorSets.data(), 0, nullptr);
  }

  {
    /* matrix multiplication bounding spheres compute shader, global data */
    VkDescriptorBufferInfo trsInfo{};
    trsInfo.buffer = renderData.rdSphereTRSMatrixBuffer.buffer;
    trsInfo.offset = 0;
    trsInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdSphereBoneMatrixBuffer.buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet trsWriteDescriptorSet{};
    trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    trsWriteDescriptorSet.dstSet = renderData.rdAssimpComputeSphereMatrixMultDescriptorSet;
    trsWriteDescriptorSet.dstBinding = 0;
    trsWriteDescriptorSet.descriptorCount = 1;
    trsWriteDescriptorSet.pBufferInfo = &trsInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpComputeSphereMatrixMultDescriptorSet;
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    std::vector<VkWriteDescriptorSet> matrixMultWriteDescriptorSets =
      { trsWriteDescriptorSet, boneMatrixWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(matrixMultWriteDescriptorSets.size()),
       matrixMultWriteDescriptorSets.data(), 0, nullptr);
  }

  {
    /* bounding spheres compute shader, global data */
    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdSphereBoneMatrixBuffer.buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdSphereModelRootMatrixBuffer.buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boundingSphereInfo{};
    boundingSphereInfo.buffer = renderData.rdBoundingSphereBuffer.buffer;
    boundingSphereInfo.offset = 0;
    boundingSphereInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpComputeBoundingSpheresDescriptorSet;
    boneMatrixWriteDescriptorSet.dstBinding = 0;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    VkWriteDescriptorSet worldPosWriteDescriptorSet{};
    worldPosWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    worldPosWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    worldPosWriteDescriptorSet.dstSet = renderData.rdAssimpComputeBoundingSpheresDescriptorSet;
    worldPosWriteDescriptorSet.dstBinding = 1;
    worldPosWriteDescriptorSet.descriptorCount = 1;
    worldPosWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet boundingSphereWriteDescriptorSet{};
    boundingSphereWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boundingSphereWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boundingSphereWriteDescriptorSet.dstSet = renderData.rdAssimpComputeBoundingSpheresDescriptorSet;
    boundingSphereWriteDescriptorSet.dstBinding = 2;
    boundingSphereWriteDescriptorSet.descriptorCount = 1;
    boundingSphereWriteDescriptorSet.pBufferInfo = &boundingSphereInfo;

    std::vector<VkWriteDescriptorSet> boundingSphereWriteDescriptorSets =
      { boneMatrixWriteDescriptorSet, worldPosWriteDescriptorSet, boundingSphereWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(boundingSphereWriteDescriptorSets.size()),
       boundingSphereWriteDescriptorSets.data(), 0, nullptr);
  }

  {
    /* sphere-drawing shader */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boundingSpheresInfo{};
    boundingSpheresInfo.buffer = renderData.rdBoundingSphereBuffer.buffer;
    boundingSpheresInfo.offset = 0;
    boundingSpheresInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdSphereDescriptorSet;
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet boundingSpheresWriteDescriptorSet{};
    boundingSpheresWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boundingSpheresWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boundingSpheresWriteDescriptorSet.dstSet = renderData.rdSphereDescriptorSet;
    boundingSpheresWriteDescriptorSet.dstBinding = 1;
    boundingSpheresWriteDescriptorSet.descriptorCount = 1;
    boundingSpheresWriteDescriptorSet.pBufferInfo = &boundingSpheresInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets =
    { matrixWriteDescriptorSet, boundingSpheresWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }
}

void VkHelper::updateIKComputeDescriptorSets(VkRenderData& renderData) {
  Logger::log(1, "%s: updating IK descriptor sets\n", __FUNCTION__);
  {
    /* matrix multiplication inverse kinematics compute shader, global data */
    VkDescriptorBufferInfo trsInfo{};
    trsInfo.buffer = renderData.rdIKTRSMatrixBuffer.buffer;
    trsInfo.offset = 0;
    trsInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdIKBoneMatrixBuffer.buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet trsWriteDescriptorSet{};
    trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    trsWriteDescriptorSet.dstSet = renderData.rdAssimpComputeIKDescriptorSet;
    trsWriteDescriptorSet.dstBinding = 0;
    trsWriteDescriptorSet.descriptorCount = 1;
    trsWriteDescriptorSet.pBufferInfo = &trsInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpComputeIKDescriptorSet;
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    std::vector<VkWriteDescriptorSet> matrixMultWriteDescriptorSets =
      { trsWriteDescriptorSet, boneMatrixWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(matrixMultWriteDescriptorSets.size()),
       matrixMultWriteDescriptorSets.data(), 0, nullptr);
  }
}

void VkHelper::updateImageDescriptorSets(VkRenderData& renderData) {
  {
    /* composite */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo colorInfo{};
    colorInfo.imageLayout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR;
    colorInfo.imageView = renderData.rdGBuffer.color.imageView;
    colorInfo.sampler = VK_NULL_HANDLE;

    VkDescriptorImageInfo positionInfo{};
    positionInfo.imageLayout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR;
    positionInfo.imageView = renderData.rdGBuffer.depth.imageView;
    positionInfo.sampler = VK_NULL_HANDLE;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR;
    normalInfo.imageView = renderData.rdGBuffer.normal.imageView;
    normalInfo.sampler = VK_NULL_HANDLE;

    VkDescriptorImageInfo selectionInfo{};
    selectionInfo.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    selectionInfo.imageView = renderData.rdSelectionImageData.imageView;
    selectionInfo.sampler = VK_NULL_HANDLE;

    VkDescriptorImageInfo ssaoColorInfo{};
    ssaoColorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ssaoColorInfo.imageView = renderData.rdSSAOColorBufferData.imageView;
    ssaoColorInfo.sampler = renderData.rdSSAOColorBufferData.sampler;

    VkDescriptorImageInfo ssaoBlurInfo{};
    ssaoBlurInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ssaoBlurInfo.imageView = renderData.rdSSAOBlurBufferData.imageView;
    ssaoBlurInfo.sampler = renderData.rdSSAOBlurBufferData.sampler;

    VkWriteDescriptorSet matrixWrite{};
    matrixWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWrite.descriptorCount = 1;
    matrixWrite.dstSet = renderData.rdCompositeDescriptorSet;
    matrixWrite.dstBinding = 0;
    matrixWrite.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet colorWrite{};
    colorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    colorWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    colorWrite.descriptorCount = 1;
    colorWrite.dstSet = renderData.rdCompositeDescriptorSet;
    colorWrite.dstBinding = 1;
    colorWrite.pImageInfo = &colorInfo;

    VkWriteDescriptorSet positionWrite{};
    positionWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    positionWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    positionWrite.descriptorCount = 1;
    positionWrite.dstSet = renderData.rdCompositeDescriptorSet;
    positionWrite.dstBinding = 2;
    positionWrite.pImageInfo = &positionInfo;

    VkWriteDescriptorSet normalWrite{};
    normalWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    normalWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    normalWrite.descriptorCount = 1;
    normalWrite.dstSet = renderData.rdCompositeDescriptorSet;
    normalWrite.dstBinding = 3;
    normalWrite.pImageInfo = &normalInfo;

    VkWriteDescriptorSet selectionWrite{};
    selectionWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    selectionWrite.descriptorCount = 1;
    selectionWrite.dstSet = renderData.rdCompositeDescriptorSet;
    selectionWrite.dstBinding = 4;
    selectionWrite.pImageInfo = &selectionInfo;

    VkWriteDescriptorSet ssaoColorWrite{};
    ssaoColorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoColorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoColorWrite.descriptorCount = 1;
    ssaoColorWrite.dstSet = renderData.rdCompositeDescriptorSet;
    ssaoColorWrite.dstBinding = 5;
    ssaoColorWrite.pImageInfo = &ssaoColorInfo;

    VkWriteDescriptorSet ssaoBlurWrite{};
    ssaoBlurWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoBlurWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoBlurWrite.descriptorCount = 1;
    ssaoBlurWrite.dstSet = renderData.rdCompositeDescriptorSet;
    ssaoBlurWrite.dstBinding = 6;
    ssaoBlurWrite.pImageInfo = &ssaoBlurInfo;

    std::vector<VkWriteDescriptorSet> writeSets = {
      matrixWrite,
      colorWrite,
      positionWrite,
      normalWrite,
      selectionWrite,
      ssaoColorWrite,
      ssaoBlurWrite
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeSets.size()), writeSets.data(), 0, nullptr);
  }

  {
    /* SSAO */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo ssaoSettingsInfo{};
    ssaoSettingsInfo.buffer = renderData.rdSSAOSettingsUBO.buffer;
    ssaoSettingsInfo.offset = 0;
    ssaoSettingsInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo depthImageInfo{};
    depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ;
    depthImageInfo.imageView = renderData.rdGBuffer.depth.imageView;
    depthImageInfo.sampler = renderData.rdGBuffer.depth.sampler;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ;
    normalInfo.imageView = renderData.rdGBuffer.normal.imageView;
    normalInfo.sampler = renderData.rdGBuffer.normal.sampler;

    VkDescriptorImageInfo ssaoNoiseInfo{};
    ssaoNoiseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ssaoNoiseInfo.imageView = renderData.rdSSAONoiseBufferData.imageView;
    ssaoNoiseInfo.sampler = renderData.rdSSAONoiseBufferData.sampler;

    VkDescriptorBufferInfo ssaoKernelInfo{};
    ssaoKernelInfo.buffer = renderData.rdSSAOKernelSamplesUBO.buffer;
    ssaoKernelInfo.offset = 0;
    ssaoKernelInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWrite{};
    matrixWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWrite.descriptorCount = 1;
    matrixWrite.dstSet = renderData.rdSSAODescriptorSet;
    matrixWrite.dstBinding = 0;
    matrixWrite.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet ssaoSettingsWrite{};
    ssaoSettingsWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoSettingsWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ssaoSettingsWrite.descriptorCount = 1;
    ssaoSettingsWrite.dstSet = renderData.rdSSAODescriptorSet;
    ssaoSettingsWrite.dstBinding = 1;
    ssaoSettingsWrite.pBufferInfo = &ssaoSettingsInfo;

    VkWriteDescriptorSet depthImageWrite{};
    depthImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    depthImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageWrite.descriptorCount = 1;
    depthImageWrite.dstSet = renderData.rdSSAODescriptorSet;
    depthImageWrite.dstBinding = 2;
    depthImageWrite.pImageInfo = &depthImageInfo;

    VkWriteDescriptorSet normalWrite{};
    normalWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    normalWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalWrite.descriptorCount = 1;
    normalWrite.dstSet = renderData.rdSSAODescriptorSet;
    normalWrite.dstBinding = 3;
    normalWrite.pImageInfo = &normalInfo;

    VkWriteDescriptorSet ssaoNoiseWrite{};
    ssaoNoiseWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoNoiseWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoNoiseWrite.descriptorCount = 1;
    ssaoNoiseWrite.dstSet = renderData.rdSSAODescriptorSet;
    ssaoNoiseWrite.dstBinding = 4;
    ssaoNoiseWrite.pImageInfo = &ssaoNoiseInfo;

    VkWriteDescriptorSet ssaoKernelWrite{};
    ssaoKernelWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoKernelWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ssaoKernelWrite.descriptorCount = 1;
    ssaoKernelWrite.dstSet = renderData.rdSSAODescriptorSet;
    ssaoKernelWrite.dstBinding = 5;
    ssaoKernelWrite.pBufferInfo = &ssaoKernelInfo;

    std::vector<VkWriteDescriptorSet> writeSets = {
      matrixWrite,
      ssaoSettingsWrite,
      depthImageWrite,
      normalWrite,
      ssaoNoiseWrite,
      ssaoKernelWrite };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeSets.size()), writeSets.data(), 0, nullptr);
  }

  {
    /* SSAO blur */
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBO.buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo ssaoSettingsInfo{};
    ssaoSettingsInfo.buffer = renderData.rdSSAOSettingsUBO.buffer;
    ssaoSettingsInfo.offset = 0;
    ssaoSettingsInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo positionInfo{};
    positionInfo.imageLayout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ;
    positionInfo.imageView = renderData.rdGBuffer.depth.imageView;
    positionInfo.sampler = renderData.rdGBuffer.depth.sampler;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ;
    normalInfo.imageView = renderData.rdGBuffer.normal.imageView;
    normalInfo.sampler = renderData.rdGBuffer.normal.sampler;

    VkDescriptorImageInfo ssaoColorInfo{};
    ssaoColorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ssaoColorInfo.imageView = renderData.rdSSAOColorBufferData.imageView;
    ssaoColorInfo.sampler = renderData.rdSSAOColorBufferData.sampler;

    VkWriteDescriptorSet matrixWrite{};
    matrixWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWrite.descriptorCount = 1;
    matrixWrite.dstSet = renderData.rdSSAOBlurDescriptorSet;
    matrixWrite.dstBinding = 0;
    matrixWrite.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet ssaoSettingsWrite{};
    ssaoSettingsWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoSettingsWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ssaoSettingsWrite.descriptorCount = 1;
    ssaoSettingsWrite.dstSet = renderData.rdSSAOBlurDescriptorSet;
    ssaoSettingsWrite.dstBinding = 1;
    ssaoSettingsWrite.pBufferInfo = &ssaoSettingsInfo;

    VkWriteDescriptorSet depthImageWrite{};
    depthImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    depthImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageWrite.descriptorCount = 1;
    depthImageWrite.dstSet = renderData.rdSSAOBlurDescriptorSet;
    depthImageWrite.dstBinding = 2;
    depthImageWrite.pImageInfo = &positionInfo;

    VkWriteDescriptorSet normalWrite{};
    normalWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    normalWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalWrite.descriptorCount = 1;
    normalWrite.dstSet = renderData.rdSSAOBlurDescriptorSet;
    normalWrite.dstBinding = 3;
    normalWrite.pImageInfo = &normalInfo;

    VkWriteDescriptorSet ssaoColorWrite{};
    ssaoColorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoColorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoColorWrite.descriptorCount = 1;
    ssaoColorWrite.dstSet = renderData.rdSSAOBlurDescriptorSet;
    ssaoColorWrite.dstBinding = 4;
    ssaoColorWrite.pImageInfo = &ssaoColorInfo;

    std::vector<VkWriteDescriptorSet> writeSets = {
      matrixWrite,
      ssaoSettingsWrite,
      depthImageWrite,
      normalWrite,
      ssaoColorWrite
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeSets.size()), writeSets.data(), 0, nullptr);
  }
}

bool VkHelper::createPipelineLayouts(VkRenderData& renderData) {
  /* non-animated model */
  std::vector<VkDescriptorSetLayout> layouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpDescriptorLayout };

  std::vector<VkPushConstantRange> pushConstants = { { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkPushConstants) } };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpPipelineLayout, layouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* animated model, needs push constant */
  std::vector<VkDescriptorSetLayout> skinningLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpSkinningDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpSkinningPipelineLayout, skinningLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp Skinning pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* selection, non-animated */
  std::vector<VkDescriptorSetLayout> selectionLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpSelectionDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpSelectionPipelineLayout, selectionLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp selection pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* selection, animated */
  std::vector<VkDescriptorSetLayout> skinningSelectionLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpSkinningSelectionDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpSkinningSelectionPipelineLayout, skinningSelectionLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp skinning selection pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* animated model plus morph */
  std::vector<VkDescriptorSetLayout> skinningMorphLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpSkinningMorphDescriptorLayout,
    renderData.rdAssimpSkinningMorphPerModelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpSkinningMorphPipelineLayout, skinningMorphLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp morph skinning pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* selection, animated, morphs */
  std::vector<VkDescriptorSetLayout> skinningMorphSelectionLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpSkinningMorphSelectionDescriptorLayout,
    renderData.rdAssimpSkinningMorphPerModelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpSkinningMorphSelectionPipelineLayout, skinningMorphSelectionLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp morph skinning selection pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* level */
  std::vector<VkDescriptorSetLayout> levelLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpLevelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpLevelPipelineLayout, levelLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp Level pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* ground mesh drawing */
  std::vector<VkDescriptorSetLayout> groundMeshLayouts = {
    renderData.rdGroundMeshDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdGroundMeshPipelineLayout, groundMeshLayouts)) {
    Logger::log(1, "%s error: could not init Assimp ground mesh drawing pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* transform compute */
  std::vector<VkPushConstantRange> computePushConstants = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VkComputePushConstants) } };

  std::vector<VkDescriptorSetLayout> transformLayouts = {
    renderData.rdAssimpComputeTransformDescriptorLayout,
    renderData.rdAssimpComputeTransformPerModelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpComputeTransformaPipelineLayout, transformLayouts, computePushConstants)) {
    Logger::log(1, "%s error: could not init Assimp transform compute pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* matrix mult compute */
  std::vector<VkDescriptorSetLayout> matrixMultLayouts = {
    renderData.rdAssimpComputeMatrixMultDescriptorLayout,
    renderData.rdAssimpComputeMatrixMultPerModelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpComputeMatrixMultPipelineLayout, matrixMultLayouts, computePushConstants)) {
    Logger::log(1, "%s error: could not init Assimp matrix multiplication compute pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* bounding spheres compute */
  std::vector<VkDescriptorSetLayout> boundingSpheresLayouts = {
    renderData.rdAssimpComputeBoundingSpheresDescriptorLayout,
    renderData.rdAssimpComputeBoundingSpheresPerModelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpComputeBoundingSpheresPipelineLayout, boundingSpheresLayouts, computePushConstants)) {
    Logger::log(1, "%s error: could not init Assimp bounding spheres compute pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* line drawing */
  std::vector<VkDescriptorSetLayout> lineLayouts = {
    renderData.rdLineDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdLinePipelineLayout, lineLayouts)) {
    Logger::log(1, "%s error: could not init Assimp line drawing pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* sphere drawing */
  std::vector<VkDescriptorSetLayout> sphereLayouts = {
    renderData.rdSphereDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdSpherePipelineLayout, sphereLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp sphere drawing pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* skybox */
  std::vector<VkDescriptorSetLayout> skyboxLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdSkyboxDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdSkyboxPipelineLayout, skyboxLayouts)) {
    Logger::log(1, "%s error: could not init Assimp skybox pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* composite pass of deferred shading */
  std::vector<VkDescriptorSetLayout> compositeLayout = {
    renderData.rdCompositeDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdCompositePipelineLayout, compositeLayout)) {
    Logger::log(1, "%s error: could not init Composite pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* ssao pass */
  std::vector<VkDescriptorSetLayout> ssaoLayout = {
    renderData.rdSSAODescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdSSAOPipelineLayout, ssaoLayout)) {
    Logger::log(1, "%s error: could not init SSAO pipeline layout\n", __FUNCTION__);
    return false;
  }

  /* ssao blur pass */
  std::vector<VkDescriptorSetLayout> ssaoBlurLayout = {
    renderData.rdSSAOBlurDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdSSAOBlurPipelineLayout, ssaoBlurLayout)) {
    Logger::log(1, "%s error: could not init SSAO blur pipeline layout\n", __FUNCTION__);
    return false;
  }

  return true;
}

bool VkHelper::createPipelines(VkRenderData& renderData) {
  std::vector<VkFormat> colorAttachmentFormats {
    renderData.rdVkbSwapchain.image_format,
    renderData.rdGBuffer.color.format,
    renderData.rdGBuffer.depth.format,
    renderData.rdGBuffer.normal.format,
    renderData.rdSelectionImageData.format,
    renderData.rdSSAOColorBufferData.format,
    renderData.rdSSAOBlurBufferData.format,
  };

  std::string vertexShaderFile = "shader/assimp.vert.spv";
  std::string fragmentShaderFile = "shader/assimp.frag.spv";
  if (!SkinningPipeline::init(renderData, colorAttachmentFormats, renderData.rdAssimpPipelineLayout,
      renderData.rdAssimpPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_skinning.vert.spv";
  fragmentShaderFile = "shader/assimp_skinning.frag.spv";
  if (!SkinningPipeline::init(renderData, colorAttachmentFormats, renderData.rdAssimpSkinningPipelineLayout,
      renderData.rdAssimpSkinningPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Skinning shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_selection.vert.spv";
  fragmentShaderFile = "shader/assimp_selection.frag.spv";
  if (!SkinningPipeline::init(renderData, colorAttachmentFormats, renderData.rdAssimpSelectionPipelineLayout,
      renderData.rdAssimpSelectionPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Selection shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_skinning_selection.vert.spv";
  fragmentShaderFile = "shader/assimp_skinning_selection.frag.spv";
  if (!SkinningPipeline::init(renderData, colorAttachmentFormats, renderData.rdAssimpSkinningSelectionPipelineLayout,
      renderData.rdAssimpSkinningSelectionPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Skinning Selection shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_skinning_morph.vert.spv";
  fragmentShaderFile = "shader/assimp_skinning_morph.frag.spv";
  if (!SkinningPipeline::init(renderData, colorAttachmentFormats, renderData.rdAssimpSkinningMorphPipelineLayout,
      renderData.rdAssimpSkinningMorphPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Morph Anim Skinning shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_skinning_morph_selection.vert.spv";
  fragmentShaderFile = "shader/assimp_skinning_morph_selection.frag.spv";
  if (!SkinningPipeline::init(renderData, colorAttachmentFormats, renderData.rdAssimpSkinningMorphSelectionPipelineLayout,
      renderData.rdAssimpSkinningMorphSelectionPipeline,
      vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Morph Anim Skinning Selection shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_level.vert.spv";
  fragmentShaderFile = "shader/assimp_level.frag.spv";
  if (!SkinningPipeline::init(renderData, colorAttachmentFormats, renderData.rdAssimpLevelPipelineLayout,
      renderData.rdAssimpLevelPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Level shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_groundmesh.vert.spv";
  fragmentShaderFile = "shader/assimp_groundmesh.frag.spv";
  if (!GroundMeshPipeline::init(renderData, colorAttachmentFormats, renderData.rdLinePipelineLayout,
      renderData.rdGroundMeshPipeline,
      vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Ground Mesh drawing shader pipeline\n", __FUNCTION__);
    return false;
  }

  std::string computeShaderFile = "shader/assimp_instance_transform.comp.spv";
  if (!ComputePipeline::init(renderData, renderData.rdAssimpComputeTransformaPipelineLayout,
      renderData.rdAssimpComputeTransformPipeline, computeShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Transform compute shader pipeline\n", __FUNCTION__);
    return false;
  }

  computeShaderFile = "shader/assimp_instance_matrix_mult.comp.spv";
  if (!ComputePipeline::init(renderData, renderData.rdAssimpComputeMatrixMultPipelineLayout,
      renderData.rdAssimpComputeMatrixMultPipeline, computeShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Matrix Mult compute shader pipeline\n", __FUNCTION__);
    return false;
  }

  computeShaderFile = "shader/assimp_instance_bounding_spheres.comp.spv";
  if (!ComputePipeline::init(renderData, renderData.rdAssimpComputeBoundingSpheresPipelineLayout,
      renderData.rdAssimpComputeBoundingSpheresPipeline, computeShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Bounding Spheres compute shader pipeline\n", __FUNCTION__);
    return false;
  }

  computeShaderFile = "shader/assimp_instance_headmove_transform.comp.spv";
  if (!ComputePipeline::init(renderData, renderData.rdAssimpComputeTransformaPipelineLayout,
      renderData.rdAssimpComputeHeadMoveTransformPipeline, computeShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Head Movement Transform compute shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/line.vert.spv";
  fragmentShaderFile = "shader/line.frag.spv";
  if (!LinePipeline::init(renderData, colorAttachmentFormats, renderData.rdLinePipelineLayout,
      renderData.rdLinePipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp line drawing shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/sphere_instance.vert.spv";
  fragmentShaderFile = "shader/sphere_instance.frag.spv";
  if (!LinePipeline::init(renderData, colorAttachmentFormats, renderData.rdSpherePipelineLayout,
      renderData.rdSpherePipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp sphere drawing shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/grid.vert.spv";
  fragmentShaderFile = "shader/grid.frag.spv";
  if (!GridLinePipeline::init(renderData, colorAttachmentFormats, renderData.rdLinePipelineLayout,
      renderData.rdGridLinePipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp grid line drawing shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/skybox.vert.spv";
  fragmentShaderFile = "shader/skybox.frag.spv";
  if (!SkyboxPipeline::init(renderData, colorAttachmentFormats, renderData.rdSkyboxPipelineLayout,
      renderData.rdSkyboxPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp skybox shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/composite.vert.spv";
  fragmentShaderFile = "shader/composite.frag.spv";
  if (!CompositePipeline::init(renderData, colorAttachmentFormats, renderData.rdCompositePipelineLayout,
    renderData.rdCompositePipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Composite pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/ssao.vert.spv";
  fragmentShaderFile = "shader/ssao.frag.spv";
  if (!SSAOPipeline::init(renderData, colorAttachmentFormats, renderData.rdSSAOPipelineLayout,
    renderData.rdSSAOPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init SSAO pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/ssao_blur.vert.spv";
  fragmentShaderFile = "shader/ssao_blur.frag.spv";
  if (!SSAOPipeline::init(renderData, colorAttachmentFormats, renderData.rdSSAOBlurPipelineLayout,
    renderData.rdSSAOBlurPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init SSAO blur pipeline\n", __FUNCTION__);
    return false;
  }

  return true;
}

bool VkHelper::createSyncObjects(VkRenderData& renderData) {
  if (!SyncObjects::init(renderData)) {
    Logger::log(1, "%s error: could not create sync objects\n", __FUNCTION__);
    return false;
  }
  return true;
}


bool VkHelper::createVertexBuffers(VkRenderData& renderData) {
  if (!VertexBuffer::init(renderData, renderData.rdLineVertexBuffer, 1024)) {
    Logger::log(1, "%s error: could not create line vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdSphereVertexBuffer, 1024)) {
    Logger::log(1, "%s error: could not create sphere vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdLevelAABBVertexBuffer, 1024)) {
    Logger::log(1, "%s error: could not create level AABB vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdLevelOctreeVertexBuffer, 1024)) {
    Logger::log(1, "%s error: could not create level octree vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdLevelWireframeVertexBuffer, 1024)) {
    Logger::log(1, "%s error: could not create level wireframe vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdIKLinesVertexBuffer, 1024)) {
    Logger::log(1, "%s error: could not create IK Lines vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdGroundMeshVertexBuffer, 1024)) {
    Logger::log(1, "%s error: could not create ground mesh vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdGroundMeshNeighborVertexBuffer, 1024)) {
    Logger::log(1, "%s error: could not create ground mesh neighbor triangles vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdInstancePathVertexBuffer, 1024)) {
    Logger::log(1, "%s error: could not create instance path vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdSkyboxBuffer, 1024)) {
    Logger::log(1, "%s error: could not create skybox vertex buffer\n", __FUNCTION__);
    return false;
  }

  /* Fullscreen qaud to draw buffer to screen */
  VkFullscreenQuadMesh fullQuadMesh{};
  VkFullscreenQuadVertex vert1 { {-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f} };
  VkFullscreenQuadVertex vert2 { {-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 0.0f} };
  VkFullscreenQuadVertex vert3 { { 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f} };
  VkFullscreenQuadVertex vert4 { { 1.0f,  1.0f, 0.0f, 1.0f}, {1.0f, 0.0f} };

  fullQuadMesh.vertices.emplace_back(vert1);
  fullQuadMesh.vertices.emplace_back(vert3);
  fullQuadMesh.vertices.emplace_back(vert2);

  fullQuadMesh.vertices.emplace_back(vert2);
  fullQuadMesh.vertices.emplace_back(vert3);
  fullQuadMesh.vertices.emplace_back(vert4);

  VertexBuffer::init(renderData, renderData.rdFullscreenQuadBufferData, 6);
  VertexBuffer::uploadData(renderData, renderData.rdFullscreenQuadBufferData, fullQuadMesh.vertices);

  return true;
}


bool VkHelper::createDepthBuffer(VkRenderData& renderData) {
  VkExtent3D depthImageExtent = {
    renderData.rdVkbSwapchain.extent.width,
    renderData.rdVkbSwapchain.extent.height,
    1
  };

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = renderData.rdDepthBufferData.format;
  imageInfo.extent = depthImageExtent;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

  VmaAllocationCreateInfo depthAllocInfo{};
  depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  depthAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VkResult result = vmaCreateImage(renderData.rdAllocator, &imageInfo, &depthAllocInfo, &renderData.rdDepthBufferData.image, &renderData.rdDepthBufferData.alloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate depth buffer memory (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  VkImageViewCreateInfo imageViewinfo{};
  imageViewinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewinfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewinfo.image = renderData.rdDepthBufferData.image;
  imageViewinfo.format = renderData.rdDepthBufferData.format;
  imageViewinfo.subresourceRange.baseMipLevel = 0;
  imageViewinfo.subresourceRange.levelCount = 1;
  imageViewinfo.subresourceRange.baseArrayLayer = 0;
  imageViewinfo.subresourceRange.layerCount = 1;
  imageViewinfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

  result = vkCreateImageView(renderData.rdVkbDevice.device, &imageViewinfo, nullptr, &renderData.rdDepthBufferData.imageView);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create depth buffer image view (error: %i)\n", __FUNCTION__, result);
    return false;
  }
  return true;
}

void VkHelper::cleanupDepthBuffer(VkRenderData& renderData) {
  vkDestroyImageView(renderData.rdVkbDevice.device, renderData.rdDepthBufferData.imageView, nullptr);
  vmaDestroyImage(renderData.rdAllocator, renderData.rdDepthBufferData.image, renderData.rdDepthBufferData.alloc);
}

bool VkHelper::createImages(VkRenderData& renderData) {
  if (!createImage(renderData, renderData.rdSelectionImageData)) {
    return false;
  }

  if (!createImage(renderData, renderData.rdSSAOColorBufferData)) {
    return false;
  }

  if (!createImage(renderData, renderData.rdSSAOBlurBufferData)) {
    return false;
  }

  return true;
}

void VkHelper::cleanupImages(VkRenderData& renderData) {
  cleanupImage(renderData, renderData.rdSelectionImageData);
  cleanupImage(renderData, renderData.rdSSAOColorBufferData);
  cleanupImage(renderData, renderData.rdSSAOBlurBufferData);
}

bool VkHelper::createImage(VkRenderData& renderData, VkImageData& image) {
  VkExtent3D imageExtent = {
        renderData.rdVkbSwapchain.extent.width,
        renderData.rdVkbSwapchain.extent.height,
        1
  };

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = image.format;
  imageInfo.extent = imageExtent;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

  VmaAllocationCreateInfo selectionAllocInfo{};
  selectionAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  selectionAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VkResult result = vmaCreateImage(renderData.rdAllocator, &imageInfo, &selectionAllocInfo,
    &image.image, &image.alloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate image memory (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  VkImageViewCreateInfo imageViewinfo{};
  imageViewinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewinfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewinfo.image = image.image;
  imageViewinfo.format = image.format;
  imageViewinfo.subresourceRange.baseMipLevel = 0;
  imageViewinfo.subresourceRange.levelCount = 1;
  imageViewinfo.subresourceRange.baseArrayLayer = 0;
  imageViewinfo.subresourceRange.layerCount = 1;
  imageViewinfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

  result = vkCreateImageView(renderData.rdVkbDevice.device, &imageViewinfo, nullptr, &image.imageView);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create image view (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  // Sampler for ImGui and Debug
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_NEAREST;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;

  if (vkCreateSampler(renderData.rdVkbDevice.device, &samplerInfo, nullptr, &image.sampler) != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create sampler\n", __FUNCTION__);
    return false;
  }

  return true;
}

float VkHelper::getPixelValueFromPos(VkRenderData& renderData, VkImage srcImage, unsigned int xPos, unsigned int yPos) {
  /* random default value to detect errors */
  float pixelColor = -444.0f;

  VkImage readbackImage;
  VmaAllocation readbackImageAlloc;

  /* create local image */
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = renderData.rdVkbSwapchain.extent.width;
  imageInfo.extent.height = renderData.rdVkbSwapchain.extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R32_SFLOAT;
  imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

  VmaAllocationCreateInfo imageAllocInfo{};
  imageAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
  imageAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  VkResult result = vmaCreateImage(renderData.rdAllocator, &imageInfo, &imageAllocInfo, &readbackImage,  &readbackImageAlloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate read back image image via VMA (error: %i)\n", __FUNCTION__, result);
    return pixelColor;
  }

  VkCommandBuffer readbackCommandBuffer = CommandBuffer::createSingleShotBuffer(renderData, renderData.rdCommandPool);

  VkImageSubresourceRange layoutTransferRange{};
  layoutTransferRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  layoutTransferRange.baseMipLevel = 0;
  layoutTransferRange.levelCount = 1;
  layoutTransferRange.baseArrayLayer = 0;
  layoutTransferRange.layerCount = 1;

  /* transition destination (local) image to transfer destination layout */
  VkImageMemoryBarrier layoutTransferBarrier{};
  layoutTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  layoutTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  layoutTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  layoutTransferBarrier.image = readbackImage;
  layoutTransferBarrier.subresourceRange = layoutTransferRange;
  layoutTransferBarrier.srcAccessMask = 0;
  layoutTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  /* transition source (selection) image to transfer source optimal layout */
  VkImageMemoryBarrier srcLayoutTransferBarrier{};
  srcLayoutTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  srcLayoutTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  srcLayoutTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  srcLayoutTransferBarrier.image = srcImage;
  srcLayoutTransferBarrier.subresourceRange = layoutTransferRange;
  srcLayoutTransferBarrier.srcAccessMask = 0;
  srcLayoutTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  /* copy selection image to local image */
  VkImageCopy imageCopyRegion{};
  imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageCopyRegion.srcSubresource.layerCount = 1;
  imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageCopyRegion.dstSubresource.layerCount = 1;
  imageCopyRegion.extent.width = renderData.rdVkbSwapchain.extent.width;
  imageCopyRegion.extent.height = renderData.rdVkbSwapchain.extent.height;
  imageCopyRegion.extent.depth = 1;

  /* transition destination (local) image to general layout to allow mapping */
  VkImageMemoryBarrier destLayoutTransferBarrier{};
  destLayoutTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  destLayoutTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  destLayoutTransferBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  destLayoutTransferBarrier.image = readbackImage;
  destLayoutTransferBarrier.subresourceRange = layoutTransferRange;
  destLayoutTransferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  destLayoutTransferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

  /* selection image back to old layout*/
  VkImageMemoryBarrier srcBackLayoutTransferBarrier{};
  srcBackLayoutTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  srcBackLayoutTransferBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  srcBackLayoutTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  srcBackLayoutTransferBarrier.image = srcImage;
  srcBackLayoutTransferBarrier.subresourceRange = layoutTransferRange;
  srcBackLayoutTransferBarrier.srcAccessMask = 0;
  srcBackLayoutTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &layoutTransferBarrier);
  vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &srcLayoutTransferBarrier);
  vkCmdCopyImage(readbackCommandBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 readbackImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopyRegion);
  vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &destLayoutTransferBarrier);
  vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &srcBackLayoutTransferBarrier);

  bool commandResult = CommandBuffer::submitSingleShotBuffer(renderData, renderData.rdCommandPool,
                                                             readbackCommandBuffer, renderData.rdGraphicsQueue);

  if (!commandResult) {
    Logger::log(1, "%s error: could not submit readback transfer commands\n", __FUNCTION__);
    return pixelColor;
  }

  /* get image layout */
  VkImageSubresource subResource{};
  subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  VkSubresourceLayout subResourceLayout{};

  vkGetImageSubresourceLayout(renderData.rdVkbDevice.device, readbackImage, &subResource, &subResourceLayout);

  /* map and read data */
  const float* data;
  result = vmaMapMemory(renderData.rdAllocator, readbackImageAlloc, (void**)&data);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not map readback image memory (error: %i)\n", __FUNCTION__, result);
    return pixelColor;
  }

  data += yPos * subResourceLayout.rowPitch / sizeof(float) + xPos;
  pixelColor = *data;

  vmaUnmapMemory(renderData.rdAllocator, readbackImageAlloc);

  /* destroy local image, no longer needed */
  vmaDestroyImage(renderData.rdAllocator, readbackImage, readbackImageAlloc);

  return pixelColor;
}

void VkHelper::cleanupImage(VkRenderData& renderData, VkImageData& image) {
  vkDestroySampler(renderData.rdVkbDevice.device, image.sampler, nullptr);
  vkDestroyImageView(renderData.rdVkbDevice.device, image.imageView, nullptr);
  vmaDestroyImage(renderData.rdAllocator, image.image, image.alloc);
}

bool VkHelper::createGBuffer(VkRenderData& renderData) {
  /* init framebuffer attachments */
  Logger::log(1, "%s: create GBuffer color attachment (RGBA, 4x 8 bit int)\n", __FUNCTION__);
  if (!FramebufferAttachment::init(renderData, renderData.rdGBuffer.color, VK_FORMAT_B8G8R8A8_UNORM,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
    return false;
  }
  Logger::log(1, "%s: create GBuffer depth attachment (1x 32 bit float)\n", __FUNCTION__);
  if (!FramebufferAttachment::init(renderData, renderData.rdGBuffer.depth, VK_FORMAT_R32_SFLOAT,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
    return false;
  }
  Logger::log(1, "%s: create GBuffer normal attachment (4x 8 bit int)\n", __FUNCTION__);
  if (!FramebufferAttachment::init(renderData, renderData.rdGBuffer.normal, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
    return false;
  }

  return true;
}

void VkHelper::cleanupGBuffer(VkRenderData& renderData) {
  if (renderData.rdGBuffer.color.image != VK_NULL_HANDLE) {
    FramebufferAttachment::cleanup(renderData, renderData.rdGBuffer.color);
    Logger::log(1, "%s: deleted GBuffer color attachment\n", __FUNCTION__);
  } else {
    Logger::log(1,"%s error: GBuffer color attachment is null\n",  __FUNCTION__);
  }
  if (renderData.rdGBuffer.depth.image != VK_NULL_HANDLE) {
    FramebufferAttachment::cleanup(renderData, renderData.rdGBuffer.depth);
    Logger::log(1, "%s: deleted GBuffer depth attachment\n", __FUNCTION__);
  } else {
    Logger::log(1,"%s error: GBuffer depth attachment is null\n",  __FUNCTION__);
  }
  if (renderData.rdGBuffer.normal.image != VK_NULL_HANDLE) {
    FramebufferAttachment::cleanup(renderData, renderData.rdGBuffer.normal);
    Logger::log(1, "%s: deleted GBuffer normal attachment\n", __FUNCTION__);
  } else {
    Logger::log(1,"%s error: GBuffer normal attachment is null\n",  __FUNCTION__);
  }
}

void VkHelper::initSSAO(VkRenderData& renderData) {
  /* samples for kernel  */
  std::vector<glm::vec4> ssaoKernel;
  std::random_device randomDevice{};
  unsigned int seed = randomDevice();
  std::default_random_engine randomEngine(seed);

  std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f); // random floats between [0.0, 1.0]

  auto lerp = [](float a, float b, float f) {
    return a + f * (b - 1);
  };

  for (unsigned int i = 0; i < 64; ++i)  {
    glm::vec4 sample(
      randomFloats(randomEngine) * 2.0f - 1.0f,
      randomFloats(randomEngine) * 2.0f - 1.0f,
      randomFloats(randomEngine), 0.0f
    );

    sample  = glm::normalize(sample);
    sample *= randomFloats(randomEngine);

    float scale = static_cast<float>(i) / 64.0;
    scale   = lerp(0.1f, 1.0f, scale * scale);
    sample *= scale;

    ssaoKernel.emplace_back(sample);
  }

  UniformBuffer::uploadData(renderData, renderData.rdSSAOKernelSamplesUBO, ssaoKernel);

  /* noise for texture */
  std::vector<glm::vec4> ssaoNoise;
  for (unsigned int i = 0; i < 16; i++)  {
    glm::vec4 noise(
      randomFloats(randomEngine) * 2.0f - 1.0f,
      randomFloats(randomEngine) * 2.0f - 1.0f,
      0.0f, 1.0f);

    ssaoNoise.push_back(noise);
  }

  createSSAONoiseTexture(renderData, ssaoNoise);
}

bool VkHelper::createSSAONoiseTexture(VkRenderData& renderData, std::vector<glm::vec4> noiseData) {
  /* noise texture is 4x4 pixels */
  VkExtent3D imageExtent = {
        4,
        4,
        1
  };

  int32_t imageSize = imageExtent.width * imageExtent.height * imageExtent.depth * sizeof(glm::vec4);

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = renderData.rdSSAONoiseBufferData.format;
  imageInfo.extent = imageExtent;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

  VmaAllocationCreateInfo ssaoAllocInfo{};
  ssaoAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  ssaoAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VkResult result = vmaCreateImage(renderData.rdAllocator, &imageInfo, &ssaoAllocInfo,
    &renderData.rdSSAONoiseBufferData.image, &renderData.rdSSAONoiseBufferData.alloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate image memory (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  /* staging buffer */
  VkBufferCreateInfo stagingBufferInfo{};
  stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  stagingBufferInfo.size = imageSize;
  stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

  VkBuffer stagingBuffer;
  VmaAllocation stagingBufferAlloc;

  VmaAllocationCreateInfo stagingAllocInfo{};
  stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

  result = vmaCreateBuffer(renderData.rdAllocator, &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer,  &stagingBufferAlloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate texture staging buffer via VMA (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  void* uploadData;
  result = vmaMapMemory(renderData.rdAllocator, stagingBufferAlloc, &uploadData);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not map texture memory (error: %i)\n", __FUNCTION__, result);
    return false;
  }
  std::memcpy(uploadData, noiseData.data(), imageSize);
  vmaUnmapMemory(renderData.rdAllocator, stagingBufferAlloc);
  vmaFlushAllocation(renderData.rdAllocator, stagingBufferAlloc, 0, imageSize);

  VkImageSubresourceRange stagingBufferRange{};
  stagingBufferRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  stagingBufferRange.baseMipLevel = 0;
  stagingBufferRange.levelCount = 1;
  stagingBufferRange.baseArrayLayer = 0;
  stagingBufferRange.layerCount = 1;

  /* 1st barrier, undefined to transfer optimal */
  VkImageMemoryBarrier stagingBufferTransferBarrier{};
  stagingBufferTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  stagingBufferTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  stagingBufferTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  stagingBufferTransferBarrier.image = renderData.rdSSAONoiseBufferData.image;
  stagingBufferTransferBarrier.subresourceRange = stagingBufferRange;
  stagingBufferTransferBarrier.srcAccessMask = 0;
  stagingBufferTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  VkBufferImageCopy stagingBufferCopy{};
  stagingBufferCopy.bufferOffset = 0;
  stagingBufferCopy.bufferRowLength = 0;
  stagingBufferCopy.bufferImageHeight = 0;
  stagingBufferCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  stagingBufferCopy.imageSubresource.mipLevel = 0;
  stagingBufferCopy.imageSubresource.baseArrayLayer = 0;
  stagingBufferCopy.imageSubresource.layerCount = 1;
  stagingBufferCopy.imageExtent = imageExtent;

  /* 2nd barrier, transfer optimal to shader optimal */
  VkImageMemoryBarrier stagingBufferShaderBarrier{};
  stagingBufferShaderBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  stagingBufferShaderBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  stagingBufferShaderBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  stagingBufferShaderBarrier.image = renderData.rdSSAONoiseBufferData.image;
  stagingBufferShaderBarrier.subresourceRange = stagingBufferRange;
  stagingBufferShaderBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  stagingBufferShaderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  VkCommandBuffer uploadCommandBuffer = CommandBuffer::createSingleShotBuffer(renderData, renderData.rdCommandPool);

  vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &stagingBufferTransferBarrier);
  vkCmdCopyBufferToImage(uploadCommandBuffer, stagingBuffer, renderData.rdSSAONoiseBufferData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &stagingBufferCopy);
  vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &stagingBufferShaderBarrier);

  bool commandResult = CommandBuffer::submitSingleShotBuffer(renderData, renderData.rdCommandPool, uploadCommandBuffer, renderData.rdGraphicsQueue);

  vmaDestroyBuffer(renderData.rdAllocator, stagingBuffer, stagingBufferAlloc);

  if (!commandResult) {
    Logger::log(1, "%s error: could not submit texture transfer commands\n", __FUNCTION__);
    return false;
  }

  VkImageViewCreateInfo imageViewinfo{};
  imageViewinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewinfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewinfo.image = renderData.rdSSAONoiseBufferData.image;
  imageViewinfo.format = renderData.rdSSAONoiseBufferData.format;
  imageViewinfo.subresourceRange.baseMipLevel = 0;
  imageViewinfo.subresourceRange.levelCount = 1;
  imageViewinfo.subresourceRange.baseArrayLayer = 0;
  imageViewinfo.subresourceRange.layerCount = 1;
  imageViewinfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

  result = vkCreateImageView(renderData.rdVkbDevice.device, &imageViewinfo, nullptr, &renderData.rdSSAONoiseBufferData.imageView);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create image view (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  // Sampler for ImGui and Debug
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_NEAREST;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;

  if (vkCreateSampler(renderData.rdVkbDevice.device, &samplerInfo, nullptr, &renderData.rdSSAONoiseBufferData.sampler) != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create sampler\n", __FUNCTION__);
    return false;
  }

  return true;
}

void VkHelper::cleanupSSAONoiseTexture(VkRenderData& renderData) {
  vkDestroySampler(renderData.rdVkbDevice.device, renderData.rdSSAONoiseBufferData.sampler, nullptr);
  vkDestroyImageView(renderData.rdVkbDevice.device, renderData.rdSSAONoiseBufferData.imageView, nullptr);
  vmaDestroyImage(renderData.rdAllocator, renderData.rdSSAONoiseBufferData.image, renderData.rdSSAONoiseBufferData.alloc);
}

void VkHelper::runComputeShaders(VkRenderData& renderData, std::shared_ptr<AssimpModel> model, int numInstances, uint32_t modelOffset, uint32_t instanceOffset, bool useEmptyBoneOffsets) {
  uint32_t numberOfBones = static_cast<uint32_t>(model->getBoneList().size());

  /* node transformation */
  if (model->hasHeadMovementAnimationsMapped()) {
    vkCmdBindPipeline(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
      renderData.rdAssimpComputeHeadMoveTransformPipeline);
  } else {
    vkCmdBindPipeline(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
      renderData.rdAssimpComputeTransformPipeline);
  }

  VkDescriptorSet &modelTransformDescriptorSet = model->getTransformDescriptorSet();
  std::vector<VkDescriptorSet> transformComputeSets = { renderData.rdAssimpComputeTransformDescriptorSet, modelTransformDescriptorSet };
  vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeTransformaPipelineLayout, 0, static_cast<uint32_t>(transformComputeSets.size()), transformComputeSets.data(), 0, 0);

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = modelOffset;
  renderData.rdComputeModelData.pkInstanceOffset = instanceOffset;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers[renderData.currentFrame], renderData.rdAssimpComputeTransformaPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers[renderData.currentFrame], numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier between the compute shaders
   * wait for TRS buffer to be written  */
  VkMemoryBarrier trsBufferBarrier{};
  trsBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  trsBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  trsBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
    &trsBufferBarrier, 0, nullptr, 0, nullptr);

  /* matrix multiplication */
  vkCmdBindPipeline(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeMatrixMultPipeline);

  if (useEmptyBoneOffsets) {
    VkDescriptorSet &modelMatrixMultDescriptorSet = model->getMatrixMultEmptyOffsetDescriptorSet();
    std::vector<VkDescriptorSet> matrixMultComputeSets =
      { renderData.rdAssimpComputeMatrixMultDescriptorSet, modelMatrixMultDescriptorSet };
    vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
      renderData.rdAssimpComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(matrixMultComputeSets.size()), matrixMultComputeSets.data(), 0, 0);
  } else {
    VkDescriptorSet &modelMatrixMultDescriptorSet = model->getMatrixMultDescriptorSet();
    std::vector<VkDescriptorSet> matrixMultComputeSets =
      { renderData.rdAssimpComputeMatrixMultDescriptorSet, modelMatrixMultDescriptorSet };
    vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
      renderData.rdAssimpComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(matrixMultComputeSets.size()), matrixMultComputeSets.data(), 0, 0);
  }

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = modelOffset;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers[renderData.currentFrame], renderData.rdAssimpComputeMatrixMultPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers[renderData.currentFrame], numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier after compute shader
   * wait for bone matrix buffer to be written  */
  VkMemoryBarrier boneMatrixBufferBarrier{};
  boneMatrixBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  boneMatrixBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  boneMatrixBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
    &boneMatrixBufferBarrier, 0, nullptr, 0, nullptr);
}

void VkHelper::runBoundingSphereComputeShaders(VkRenderData& renderData, std::shared_ptr<AssimpModel> model, int numInstances,
    uint32_t modelOffset) {
  uint32_t numberOfBones = static_cast<uint32_t>(model->getBoneList().size());

  /* node transformation */
  vkCmdBindPipeline(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeTransformPipeline);

  VkDescriptorSet &modelTransformDescriptorSet = model->getTransformDescriptorSet();
  std::vector<VkDescriptorSet> transformComputeSets = { renderData.rdAssimpComputeSphereTransformDescriptorSet, modelTransformDescriptorSet };
  vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeTransformaPipelineLayout, 0, static_cast<uint32_t>(transformComputeSets.size()), transformComputeSets.data(), 0, 0);

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = 0;
  renderData.rdComputeModelData.pkInstanceOffset = 0;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers[renderData.currentFrame], renderData.rdAssimpComputeTransformaPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers[renderData.currentFrame], numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier between the compute shaders
   * wait for TRS buffer to be written  */
  VkMemoryBarrier trsBufferBarrier{};
  trsBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  trsBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  trsBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
    &trsBufferBarrier, 0, nullptr, 0, nullptr);

  /* matrix multiplication */
  vkCmdBindPipeline(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeMatrixMultPipeline);

  VkDescriptorSet &modelMatrixMultDescriptorSet = model->getMatrixMultEmptyOffsetDescriptorSet();
  std::vector<VkDescriptorSet> matrixMultComputeSets =
    { renderData.rdAssimpComputeSphereMatrixMultDescriptorSet, modelMatrixMultDescriptorSet };
  vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(matrixMultComputeSets.size()), matrixMultComputeSets.data(), 0, 0);

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = 0;
  renderData.rdComputeModelData.pkInstanceOffset = 0;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers[renderData.currentFrame], renderData.rdAssimpComputeMatrixMultPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers[renderData.currentFrame], numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier after compute shader
   * wait for bone matrix buffer to be written  */
  VkMemoryBarrier boneMatrixBufferBarrier{};
  boneMatrixBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  boneMatrixBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  boneMatrixBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
    &boneMatrixBufferBarrier, 0, nullptr, 0, nullptr);

  vkCmdBindPipeline(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
                    renderData.rdAssimpComputeBoundingSpheresPipeline);

  VkDescriptorSet &boundingSpheresDescriptorSet = model->getBoundingSphereDescriptorSet();
  std::vector<VkDescriptorSet> boundingSphereComputeSets = { renderData.rdAssimpComputeBoundingSpheresDescriptorSet, boundingSpheresDescriptorSet };
  vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeBoundingSpheresPipelineLayout, 0, static_cast<uint32_t>(boundingSphereComputeSets.size()), boundingSphereComputeSets.data(), 0, 0);

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = modelOffset;
  renderData.rdComputeModelData.pkInstanceOffset = 0;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers[renderData.currentFrame], renderData.rdAssimpComputeBoundingSpheresPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers[renderData.currentFrame], numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier between the compute shaders
   * wait for bounding sphere buffer to be written  */
  VkMemoryBarrier boundingSphereBufferBarrier{};
  boundingSphereBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  boundingSphereBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  boundingSphereBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
    &boundingSphereBufferBarrier, 0, nullptr, 0, nullptr);
}

bool VkHelper::runIKComputeShaders(VkRenderData& renderData, std::shared_ptr<AssimpModel> model, int numInstances, uint32_t modelOffset) {
  uint32_t numberOfBones = static_cast<uint32_t>(model->getBoneList().size());

  /* upload changed TRS data of this model only */
  renderData.rdUploadToUBOTimer.start();
  ShaderStorageBuffer::uploadSsboData(renderData, renderData.rdIKTRSMatrixBuffer, renderData.rdTRSData, modelOffset);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  VkResult result = vkResetFences(renderData.rdVkbDevice.device, 1, &renderData.rdComputeFences[renderData.currentFrame]);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
    return false;
  }
  if (!CommandBuffer::reset(renderData.rdComputeCommandBuffers[renderData.currentFrame], 0)) {
    Logger::log(1, "%s error: failed to reset compute command buffer\n", __FUNCTION__);
    return false;
  }

  if (!CommandBuffer::beginSingleShot(renderData.rdComputeCommandBuffers[renderData.currentFrame])) {
    Logger::log(1, "%s error: failed to begin compute command buffer\n", __FUNCTION__);
    return false;
  }

  /* recalculate all TRS matrices */
  vkCmdBindPipeline(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeMatrixMultPipeline);

  VkDescriptorSet &modelMatrixMultDescriptorSet = model->getMatrixMultDescriptorSet();
  std::vector<VkDescriptorSet> matrixMultComputeSets =
    { renderData.rdAssimpComputeIKDescriptorSet, modelMatrixMultDescriptorSet };
  vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(matrixMultComputeSets.size()), matrixMultComputeSets.data(), 0, 0);

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = modelOffset;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers[renderData.currentFrame], renderData.rdAssimpComputeMatrixMultPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers[renderData.currentFrame], numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier after compute shader
   * wait for bone matrix buffer to be written  */
  VkMemoryBarrier boneMatrixBufferBarrier{};
  boneMatrixBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  boneMatrixBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  boneMatrixBufferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers[renderData.currentFrame], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
     &boneMatrixBufferBarrier, 0, nullptr, 0, nullptr);

  if (!CommandBuffer::end(renderData.rdComputeCommandBuffers[renderData.currentFrame])) {
    Logger::log(1, "%s error: failed to end compute command buffer\n", __FUNCTION__);
    return false;
  }

  /* submit compute commands */
  VkSubmitInfo computeSubmitInfo{};
  computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  computeSubmitInfo.commandBufferCount = 1;
  computeSubmitInfo.pCommandBuffers = &renderData.rdComputeCommandBuffers[renderData.currentFrame];

  result = vkQueueSubmit(renderData.rdComputeQueue, 1, &computeSubmitInfo, renderData.rdComputeFences[renderData.currentFrame]);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
    return false;
  };

  /* we must wait for the compute shaders to finish before we can read the data */
  result = vkWaitForFences(renderData.rdVkbDevice.device, 1, &renderData.rdComputeFences[renderData.currentFrame], VK_TRUE, UINT64_MAX);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: waiting for compute fence failed (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  /* read (new) bone positions of this model only */
  renderData.rdDownloadFromUBOTimer.start();
  renderData.rdIKModelMatrices = ShaderStorageBuffer::getSsboDataMat4(renderData, renderData.rdIKBoneMatrixBuffer,
    modelOffset, numInstances * numberOfBones);
  std::memcpy(renderData.rdIKMatrices.data() + modelOffset, renderData.rdIKModelMatrices.data(), numInstances * numberOfBones);
  renderData.rdDownloadFromUBOTime += renderData.rdDownloadFromUBOTimer.stop();

  return true;
}

void VkHelper::cleanup(VkRenderData& renderData) {
  SyncObjects::cleanup(renderData);

  for (int i = 0; i < renderData.MAX_FRAMES_IN_FLIGHT; ++i) {
    CommandBuffer::cleanup(renderData, renderData.rdCommandPool, renderData.rdCommandBuffers[i]);
    CommandBuffer::cleanup(renderData, renderData.rdComputeCommandPool, renderData.rdComputeCommandBuffers[i]);
  }
  CommandPool::cleanup(renderData, renderData.rdCommandPool);
  CommandPool::cleanup(renderData, renderData.rdComputeCommandPool);

  SkinningPipeline::cleanup(renderData, renderData.rdAssimpPipeline);
  SkinningPipeline::cleanup(renderData, renderData.rdAssimpSkinningPipeline);
  SkinningPipeline::cleanup(renderData, renderData.rdAssimpSelectionPipeline);
  SkinningPipeline::cleanup(renderData, renderData.rdAssimpSkinningSelectionPipeline);
  SkinningPipeline::cleanup(renderData, renderData.rdAssimpSkinningMorphPipeline);
  SkinningPipeline::cleanup(renderData, renderData.rdAssimpSkinningMorphSelectionPipeline);
  SkinningPipeline::cleanup(renderData, renderData.rdAssimpLevelPipeline);
  LinePipeline::cleanup(renderData, renderData.rdLinePipeline);
  LinePipeline::cleanup(renderData, renderData.rdSpherePipeline);
  GridLinePipeline::cleanup(renderData, renderData.rdGridLinePipeline);
  GroundMeshPipeline::cleanup(renderData, renderData.rdGroundMeshPipeline);
  SkyboxPipeline::cleanup(renderData, renderData.rdSkyboxPipeline);
  SkyboxPipeline::cleanup(renderData, renderData.rdCompositePipeline);
  SSAOPipeline::cleanup(renderData, renderData.rdSSAOPipeline);
  SSAOPipeline::cleanup(renderData, renderData.rdSSAOBlurPipeline);

  ComputePipeline::cleanup(renderData, renderData.rdAssimpComputeTransformPipeline);
  ComputePipeline::cleanup(renderData, renderData.rdAssimpComputeHeadMoveTransformPipeline);
  ComputePipeline::cleanup(renderData, renderData.rdAssimpComputeMatrixMultPipeline);
  ComputePipeline::cleanup(renderData, renderData.rdAssimpComputeBoundingSpheresPipeline);

  PipelineLayout::cleanup(renderData, renderData.rdAssimpPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpSkinningPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpComputeTransformaPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpComputeMatrixMultPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpComputeBoundingSpheresPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpSelectionPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpSkinningSelectionPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpSkinningMorphPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpSkinningMorphSelectionPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpLevelPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdLinePipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdSpherePipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdGroundMeshPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdSkyboxPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdCompositePipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdSSAOPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdSSAOBlurPipelineLayout);

  UniformBuffer::cleanup(renderData, renderData.rdRenderUploadDataUBO);
  UniformBuffer::cleanup(renderData, renderData.rdSSAOSettingsUBO);
  UniformBuffer::cleanup(renderData, renderData.rdSSAOKernelSamplesUBO);

  VertexBuffer::cleanup(renderData, renderData.rdLineVertexBuffer);
  VertexBuffer::cleanup(renderData, renderData.rdSphereVertexBuffer);
  VertexBuffer::cleanup(renderData, renderData.rdLevelAABBVertexBuffer);
  VertexBuffer::cleanup(renderData, renderData.rdLevelOctreeVertexBuffer);
  VertexBuffer::cleanup(renderData, renderData.rdLevelWireframeVertexBuffer);
  VertexBuffer::cleanup(renderData, renderData.rdIKLinesVertexBuffer);
  VertexBuffer::cleanup(renderData, renderData.rdGroundMeshVertexBuffer);
  VertexBuffer::cleanup(renderData, renderData.rdGroundMeshNeighborVertexBuffer);
  VertexBuffer::cleanup(renderData, renderData.rdInstancePathVertexBuffer);
  VertexBuffer::cleanup(renderData, renderData.rdSkyboxBuffer);
  VertexBuffer::cleanup(renderData, renderData.rdFullscreenQuadBufferData);

  ShaderStorageBuffer::cleanup(renderData, renderData.rdShaderTRSMatrixBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdPerInstanceAnimDataBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdShaderModelRootMatrixBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdShaderBoneMatrixBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdSelectedInstanceBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdBoundingSphereBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdSphereModelRootMatrixBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdSpherePerInstanceAnimDataBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdSphereTRSMatrixBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdSphereBoneMatrixBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdFaceAnimPerInstanceDataBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdShaderLevelRootMatrixBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdIKBoneMatrixBuffer);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdIKTRSMatrixBuffer);

  Texture::cleanup(renderData, renderData.rdSkyboxTexture);

  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpSkinningDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpComputeTransformDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpComputeMatrixMultDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpSelectionDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpSkinningSelectionDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpSkinningMorphDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpSkinningMorphSelectionDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpLevelDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdLineDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdSphereDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdGroundMeshDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdSkyboxDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdCompositeDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdSSAODescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdSSAOBlurDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpComputeSphereTransformDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpComputeSphereMatrixMultDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpComputeIKDescriptorSet);
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, 1,
    &renderData.rdAssimpComputeBoundingSpheresDescriptorSet);

  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSkinningDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpTextureDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeTransformDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeTransformPerModelDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeMatrixMultDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeMatrixMultPerModelDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeBoundingSpheresDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeBoundingSpheresPerModelDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSelectionDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSkinningSelectionDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSkinningMorphDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSkinningMorphSelectionDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSkinningMorphPerModelDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpLevelDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdLineDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdSphereDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdGroundMeshDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdSkyboxDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdCompositeDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdSSAODescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdSSAOBlurDescriptorLayout, nullptr);

  vkDestroyDescriptorPool(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, nullptr);

  cleanupGBuffer(renderData);
  cleanupDepthBuffer(renderData);
  cleanupImages(renderData);

  vmaDestroyAllocator(renderData.rdAllocator);

  renderData.rdVkbSwapchain.destroy_image_views(renderData.rdSwapchainImageViews);
  vkb::destroy_swapchain(renderData.rdVkbSwapchain);

  vkb::destroy_device(renderData.rdVkbDevice);
  vkb::destroy_surface(renderData.rdVkbInstance.instance, renderData.rdSurface);
  vkb::destroy_instance(renderData.rdVkbInstance);

  Logger::log(1, "%s: Vulkan renderer destroyed\n", __FUNCTION__);
}
