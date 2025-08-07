#pragma once

#include <vulkan/vulkan.h>

#include <VkRenderData.h>

class FramebufferAttachment {
  public:
    static bool init(VkRenderData& renderData, VkImageData& bufferData, VkFormat format, VkImageUsageFlags flags);
    static void cleanup(VkRenderData& renderData, VkImageData& bufferData);
};
