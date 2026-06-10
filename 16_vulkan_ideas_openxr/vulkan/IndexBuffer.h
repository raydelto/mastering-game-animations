// Vulkan uniform index buffer object
#pragma once

#include <vulkan/vulkan.h>

#include <VkRenderData.h>

class IndexBuffer {
  public:
    static bool init(VkRenderData &renderData, VkIndexBufferData &bufferData,
      size_t bufferSize);
    static bool uploadData(VkRenderData &renderData, VkIndexBufferData &bufferData,
      VkMesh vertexData);
    static bool uploadData(VkRenderData &renderData, VkIndexBufferData &bufferData,
      std::vector<uint32_t> indexData);

    static void cleanup(VkRenderData &renderData, VkIndexBufferData &bufferData);
};
