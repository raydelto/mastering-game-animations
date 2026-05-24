#pragma once

#include <vector>
#include <string>

#if defined(WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#else
#define VK_USE_PLATFORM_XLIB_KHR
#endif

#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>

// Define any XR_USE_PLATFORM_... / XR_USE_GRAPHICS_API_... before this header file.
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>


class VRHeadset {
  public:
    bool init(void* bindings);
    void pollEvents();
    void cleanup();

    bool isVRApplicationRunning() { return mApplicationRunning; }
    void setVRApplicationRunning(bool state) { mApplicationRunning = state; }

  private:
    bool createInstance();
    bool createSession(void* bindings);
    bool createDebugMessenger();

    bool getInstanceID();
    bool getSystemID();
    bool getViewConfigViews();
    bool getEnvBlendModes();
    bool getVulkanGraphicsRequirements();

    void destroyInstance();
    void destroySession();
    void destroyDebugMessenger();

    bool mSessionRunning = false;
    bool mApplicationRunning = false;

    XrApplicationInfo mXRAppInfo{};
    std::vector<const char *> mActiveAPILayers{};
    std::vector<const char *> mActiveInstanceExtensions{};

    std::vector<std::string> mInstanceExtensions{};
    std::vector<std::string> mAPILayers{};

    XrInstance mXRInstance = XR_NULL_HANDLE;

    XrFormFactor mFormFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId mSystemID = {};
    XrSystemProperties mSystemProperties = { XR_TYPE_SYSTEM_PROPERTIES };

    XrSession mSession{};
    XrSessionState mSessionState = XR_SESSION_STATE_UNKNOWN;

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
