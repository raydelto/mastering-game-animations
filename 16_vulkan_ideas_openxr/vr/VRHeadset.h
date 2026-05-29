#pragma once

#include <vector>
#include <string>
#include <tuple>

#if defined(WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#else
#define VK_USE_PLATFORM_XLIB_KHR
#endif

#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>

#include <VkRenderData.h>

// Define any XR_USE_PLATFORM_... / XR_USE_GRAPHICS_API_... before this header file.
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

class VRHeadset {
  public:
    bool initXR();
    bool initXRSecondHalf(VkRenderData &renderData);

    std::pair<unsigned int, unsigned int> getXRResolution();

    bool beginXRFrame(VkRenderData &renderData);
    bool renderXRFrame(VkRenderData &renderData);

    void pollEvents();
    bool isXRApplicationRunning();
    bool isXRSessionRunning();

    void cleanupXR();
    void cleanupXRSecondHalf(VkRenderData &renderData);

    bool isVRApplicationRunning() { return mApplicationRunning; }
    void setVRApplicationRunning(bool state) { mApplicationRunning = state; }

    glm::mat4 createXRProjectionMatrix(float left, float right, float bottom, float top, float nearZ, float farZ);

    std::vector<std::string> getVKDeviceExtensionsForXR();
    std::vector<std::string> getVKInstanceExtensionsForXR();

  private:
    bool createXRInstance();
    bool createXRDebugMessenger();
    bool createXRSession(VkRenderData &renderData);
    bool createXRReferenceSpace();
    bool createXRSwapchain(VkRenderData &renderData);
    bool createXRPipeline(VkRenderData &renderData);

    bool getInstanceID();
    bool getSystemID();
    bool getViewConfigViews();
    bool getEnvBlendModes();
    bool getVulkanGraphicsRequirements();
    bool getVKDeviceExtensions();
    bool getVKInstanceExtension();

    void destroyXRInstance();
    void destroyXRDebugMessenger();
    void destroyXRReferenceSpace();
    void destroyXRSession();
    void destroyXRSwapchain(VkRenderData &renderData);
    void destroyXRPipeline(VkRenderData &renderData);

    bool mSessionRunning = false;
    bool mApplicationRunning = false;

    unsigned int mWidth = 1920;
    unsigned int mHeight = 1080;

    XrApplicationInfo mXRAppInfo{};
    std::vector<const char *> mActiveAPILayers{};
    std::vector<const char *> mActiveInstanceExtensions{};

    std::vector<std::string> mInstanceExtensions{};
    std::vector<std::string> mAPILayers{};

    std::vector<std::string> mVKDeviceExtensionsForXR{};
    std::vector<std::string> mVKInstanceExtensionsForXR{};

    XrInstance mXRInstance = XR_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> mSwapchainCopyDescriptorSets{};
    VkPipeline mXRSwapchainCopyPipeline = VK_NULL_HANDLE;

    XrFormFactor mFormFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId mSystemID = {};
    XrSystemProperties mSystemProperties = { XR_TYPE_SYSTEM_PROPERTIES };

    XrSession mSession{};
    XrSessionState mSessionState = XR_SESSION_STATE_UNKNOWN;

    XrSpace mLocalSpace = XR_NULL_HANDLE;

    struct XRSwapchainInfo {
      XrSwapchain swapchain = XR_NULL_HANDLE;
      VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
      std::vector<XrSwapchainImageVulkanKHR> swapchainImages{};
      std::vector<VkImageView> swapchainImageViews{};
    };

    std::vector<XRSwapchainInfo> mSwapchains{};

    XrFrameState mFrameState{ .type = XR_TYPE_FRAME_STATE };
    std::vector<XrView> mViews{};
    uint32_t mViewCount = 0;


    struct XRRenderLayerInfo {
      XrTime predictedDisplayTime = 0;
      std::vector<XrCompositionLayerBaseHeader *> layers;
      XrCompositionLayerProjection layerProjection = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
      std::vector<XrCompositionLayerProjectionView> layerProjectionViews;
    };

    bool renderXRLayer(VkRenderData &renderData, XRRenderLayerInfo &renderLayerInfo);

    XrViewConfigurationType mViewConfiguration = XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM;
    std::vector<XrViewConfigurationType> mApplicationViewConfigurations = { XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO };
    std::vector<XrViewConfigurationType> mViewConfigurations{};
    std::vector<XrViewConfigurationView> mViewConfigurationViews{};

    XrEnvironmentBlendMode mEnvironmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM;
    std::vector<XrEnvironmentBlendMode> mApplicationEnvironmentBlendModes = { XR_ENVIRONMENT_BLEND_MODE_OPAQUE, XR_ENVIRONMENT_BLEND_MODE_ADDITIVE };
    std::vector<XrEnvironmentBlendMode> mEnvironmentBlendModes{};

    XrGraphicsRequirementsVulkanKHR mGraphicsRequirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
    XrDebugUtilsMessengerEXT mDebugMessenger{};

    static XrBool32 handleXRErrors(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT type,
      const XrDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData);
};
