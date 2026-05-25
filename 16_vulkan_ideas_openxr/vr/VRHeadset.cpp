#include <sstream>
#include <cstring>
#include <algorithm>

#include <XRCopyPipeline.h>
#include <VRHeadset.h>

#include <Logger.h>

bool VRHeadset::initXR() {
  Logger::log(1, "%s: VR Headset init start\n", __FUNCTION__);

  if (!createXRInstance()) {
    return false;
  }

  if (!createXRDebugMessenger()) {
    return false;
  }

  if (!getInstanceID()) {
    return false;
  }

  if (!getSystemID()) {
    return false;
  }

  if (!getViewConfigViews()) {
    return false;
  }

  if (!getEnvBlendModes()) {
    return false;
  }

  if (!getVulkanGraphicsRequirements()) {
    return false;
  }

  if (!getVKDeviceExtensions()) {
    return false;
  }

  if (!getVKInstanceExtension()) {
    return false;
  }

  Logger::log(1, "%s: VR Headset init success\n", __FUNCTION__);

  return true;
}

bool VRHeadset::initXRSecondHalf(VkRenderData& renderData) {
  if (!createXRSession(renderData)) {
    return false;
  }

  if (!createXRReferenceSpace()) {
    return false;
  }

  if (!createXRSwapchain(renderData)) {
    return false;
  }

  if (!createXRPipeline(renderData)) {
    return false;
  }

  Logger::log(1, "%s: VR Headset 2nd half init success\n", __FUNCTION__);

  return true;
}

void VRHeadset::cleanupXR() {
  destroyXRDebugMessenger();
  destroyXRInstance();
}

void VRHeadset::cleanupXRSecondHalf(VkRenderData &renderData) {
  destroyXRPipeline(renderData);
  destroyXRSwapchain(renderData);
  destroyXRReferenceSpace();
  destroyXRSession();
}

bool VRHeadset::isXRSessionRunning() {
  return mSessionRunning;
}

bool VRHeadset::isXRApplicationRunning() {
  return mApplicationRunning;
}


void VRHeadset::pollEvents() {
  XrEventDataBuffer eventData{};
  eventData.type = XR_TYPE_EVENT_DATA_BUFFER;

  auto XrPollEvents = [&]() -> bool {
    eventData = {};
    eventData.type = XR_TYPE_EVENT_DATA_BUFFER;

    return xrPollEvent(mXRInstance, &eventData) == XR_SUCCESS;
  };

  while (XrPollEvents()) {
    switch (eventData.type) {
      case XR_TYPE_EVENT_DATA_EVENTS_LOST:
      {
        XrEventDataEventsLost *eventsLost = reinterpret_cast<XrEventDataEventsLost*>(&eventData);
        Logger::log(1, "%s warning: Lost %i events\n", __FUNCTION__, eventsLost->lostEventCount);
        break;
      }
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
      {
        XrEventDataInstanceLossPending *instanceLossPending = reinterpret_cast<XrEventDataInstanceLossPending*>(&eventData);
        Logger::log(1, "%s error: Instance loss pending at timestamp %lli\n", __FUNCTION__, instanceLossPending->lossTime);
        mSessionRunning = false;
        mApplicationRunning = false;
        break;
      }
      case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
      {
        XrEventDataInteractionProfileChanged *interactionProfileChanged = reinterpret_cast<XrEventDataInteractionProfileChanged*>(&eventData);
        Logger::log(1, "%s info: Interaction profile changed for session %p\n", __FUNCTION__, interactionProfileChanged->session);
        if (interactionProfileChanged->session != mSession) {
          Logger::log(1, "%s error: XrEventDataInteractionProfileChanged for unknown session?!\n", __FUNCTION__);
          break;
        }
        break;
      }
      case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
      {
        XrEventDataReferenceSpaceChangePending *refSpaceChangePending = reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&eventData);
        Logger::log(1, "%s info: Interaction profile changed for session %p\n", __FUNCTION__, refSpaceChangePending->session);
        if (refSpaceChangePending->session != mSession) {
          Logger::log(1, "%s error: XrEventDataReferenceSpaceChangePending for unknown session?!\n", __FUNCTION__);
          break;
        }
        break;
      }
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
      {
        XrEventDataSessionStateChanged *sessionStateChanged = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
        if (sessionStateChanged->session != mSession) {
          Logger::log(1, "%s error: XrEventDataSessionStateChanged for unknown session?!\n", __FUNCTION__);
          break;
        }

        switch(sessionStateChanged->state) {
          case XR_SESSION_STATE_READY:
          {
            XrSessionBeginInfo sessionInfo{};
            sessionInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
            sessionInfo.primaryViewConfigurationType = mViewConfiguration;

            XrResult result = xrBeginSession(mSession, &sessionInfo);
            if (result != XR_SUCCESS) {
              Logger::log(1, "%s error: Failed to begin Session\n", __FUNCTION__);
              break;
            }
            mSessionRunning = true;
          }
          case XR_SESSION_STATE_STOPPING:
          {
            XrResult result = xrEndSession(mSession);
            if (result != XR_SUCCESS) {
              Logger::log(1, "%s error: Failed to end Session\n", __FUNCTION__);
              break;
            }
            mSessionRunning = false;
          }
          case XR_SESSION_STATE_EXITING:
          {
            mSessionRunning = false;
            mApplicationRunning = false;
          }
          case XR_SESSION_STATE_LOSS_PENDING:
          {
            // XXX: Recovery may be possible but we stop for now
            mSessionRunning = false;
            mApplicationRunning = false;
          }
          default:
            Logger::log(1, "%s warning: Found unhandled session change state %i\n", __FUNCTION__, sessionStateChanged->state);
            break;
        }

        // Store state for use in the application
        mSessionState = sessionStateChanged->state;
        break;
      }
      default:
        Logger::log(1, "%s warning: Found unhandled event %i\n", __FUNCTION__, eventData.type);
        break;
    }
  }
}

bool VRHeadset::createXRInstance() {
  std::strncpy(mXRAppInfo.applicationName, "Mastering C++ Game Animation Programming - VR", XR_MAX_APPLICATION_NAME_SIZE);
  mXRAppInfo.applicationVersion = 1;
  std::strncpy(mXRAppInfo.engineName, "Michael's Engine", XR_MAX_ENGINE_NAME_SIZE);
  mXRAppInfo.engineVersion = 1;
  // Steam still has no support for API v1.1
  mXRAppInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);

  // Get API Layers
  uint32_t apiLayerCount = 0;
  std::vector<XrApiLayerProperties> apiLayerProperties;

  XrResult result = xrEnumerateApiLayerProperties(0, &apiLayerCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1,"%s error: Failed to enumerate ApiLayerProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: found %i API layers\n", __FUNCTION__, apiLayerCount);

  apiLayerProperties.resize(apiLayerCount, {XR_TYPE_API_LAYER_PROPERTIES});
  result = xrEnumerateApiLayerProperties(apiLayerCount, &apiLayerCount, apiLayerProperties.data());
  if (result != XR_SUCCESS) {
    Logger::log(1,"%s error: Failed to enumerate ApiLayerProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  for (auto &layerProperty : apiLayerProperties) {
    Logger::log(1, "%s: -- Found layer %s (version %i)\n", __FUNCTION__, std::string(layerProperty.layerName).c_str(), layerProperty.layerVersion);
  }

  // Activate Layers
  Logger::log(1, "%s: Trying to activate layers\n", __FUNCTION__);
  mAPILayers.push_back("XR_APILAYER_LUNARG_core_validation");

  for (auto &requestedAPILayer : mAPILayers) {
    bool found = false;
    for (auto &apiLayer : apiLayerProperties) {
      // strcmp returns 0 if the strings match.
      if (strcmp(requestedAPILayer.c_str(), apiLayer.layerName) != 0) {
        continue;
      } else {
        mActiveAPILayers.push_back(requestedAPILayer.c_str());
        found = true;
        break;
      }
    }
    if (!found) {
      Logger::log(1, "%s: -- Failed to find OpenXR API Layer %s\n", __FUNCTION__, requestedAPILayer.c_str());
    }
  }

  for (auto &layer : mActiveAPILayers) {
    Logger::log(1, "%s: -- Active APY Layer: %s\n", __FUNCTION__, layer);
  }

  // Get all extensions
  uint32_t extensionCount = 0;
  std::vector<XrExtensionProperties> extensionProperties;

  result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate InstanceExtensionProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: found %i extensions\n", __FUNCTION__, extensionCount);

  extensionProperties.resize(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
  result = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensionProperties.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate InstanceExtensionProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  for (auto &ext : extensionProperties) {
    Logger::log(1, "%s: -- Found extension %s (version %i)\n", __FUNCTION__, std::string(ext.extensionName).c_str(), ext.extensionVersion);
  }

  // Activate extensions
  Logger::log(1, "%s: Trying to activate extensions\n", __FUNCTION__);
  mInstanceExtensions.push_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
  mInstanceExtensions.push_back(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);

  for (auto &requestedInstanceExtension : mInstanceExtensions) {
    bool found = false;
    for (auto &extensionProperty : extensionProperties) {
      // strcmp returns 0 if the strings match.
      if (strcmp(requestedInstanceExtension.c_str(), extensionProperty.extensionName) != 0) {
        continue;
      } else {
        mActiveInstanceExtensions.push_back(requestedInstanceExtension.c_str());
        found = true;
        break;
      }
    }
    if (!found) {
      Logger::log(1, "%s: -- Failed to find OpenXR instance extension %s\n", __FUNCTION__, requestedInstanceExtension.c_str());
    }
  }

  for (auto &ext : mActiveInstanceExtensions) {
    Logger::log(1, "%s: -- Active extension: %s\n", __FUNCTION__, ext);
  }

  // Create instance
  XrInstanceCreateInfo instanceInfo{};
  instanceInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.createFlags = 0;
  instanceInfo.applicationInfo = mXRAppInfo;
  instanceInfo.enabledApiLayerCount = static_cast<uint32_t>(mActiveAPILayers.size());
  instanceInfo.enabledApiLayerNames = mActiveAPILayers.data();
  instanceInfo.enabledExtensionCount = static_cast<uint32_t>(mActiveInstanceExtensions.size());
  instanceInfo.enabledExtensionNames = mActiveInstanceExtensions.data();

  result = xrCreateInstance(&instanceInfo, &mXRInstance);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create Instance (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  return true;
}

bool VRHeadset::getInstanceID() {
  // Instance info
  XrInstanceProperties instProperties{};
  instProperties.type = XR_TYPE_INSTANCE_PROPERTIES;
  XrResult result = xrGetInstanceProperties(mXRInstance, &instProperties);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get InstanceProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: OpenXR Runtime %s, version %i.%i.%i\n", __FUNCTION__,
    instProperties.runtimeName,
    XR_VERSION_MAJOR(instProperties.runtimeVersion),
    XR_VERSION_MINOR(instProperties.runtimeVersion),
    XR_VERSION_PATCH(instProperties.runtimeVersion)
  );

  return true;
}

bool VRHeadset::getSystemID() {
  // System info
  XrSystemGetInfo systemInfo{};
  systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
  systemInfo.formFactor = mFormFactor;

  XrResult result = xrGetSystem(mXRInstance, &systemInfo, &mSystemID);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get SystemID (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  // Get the System's properties for some general information about the hardware and the vendor.
  result = xrGetSystemProperties(mXRInstance, mSystemID, &mSystemProperties);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get SystemProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Found system '%s'\n", __FUNCTION__, mSystemProperties.systemName);

  return true;
}

bool VRHeadset::getViewConfigViews() {
  uint32_t viewConfCount = 0;
  XrResult result = xrEnumerateViewConfigurations(mXRInstance, mSystemID, 0, &viewConfCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate view configurations (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Found %i view configs\n", __FUNCTION__, viewConfCount);

  mViewConfigurations.resize(viewConfCount);
  result = xrEnumerateViewConfigurations(mXRInstance, mSystemID, viewConfCount, &viewConfCount, mViewConfigurations.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate view configurations (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  // Pick first supported view config
  for (const XrViewConfigurationType &viewConfiguration : mApplicationViewConfigurations) {
    if (std::find(mViewConfigurations.begin(), mViewConfigurations.end(), viewConfiguration) != mViewConfigurations.end()) {
      mViewConfiguration = viewConfiguration;
      break;
    }
  }

  if (mViewConfiguration == XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM) {
    Logger::log(1, "%s: Failed to find a view configuration type. Defaulting to XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO.", __FUNCTION__);
    mViewConfiguration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  }

  Logger::log(1, "%s: Using view configuration %i\n", __FUNCTION__, mViewConfiguration);

  uint32_t viewConfViewCount = 0;
  result = xrEnumerateViewConfigurationViews(mXRInstance, mSystemID, mViewConfiguration, 0, &viewConfViewCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate view configuration views (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Found %i view confuration views\n", __FUNCTION__, viewConfViewCount);
  
  mViewConfigurationViews.resize(viewConfViewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
  result = xrEnumerateViewConfigurationViews(mXRInstance, mSystemID, mViewConfiguration, viewConfViewCount, &viewConfViewCount, mViewConfigurationViews.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate view configuration views (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  return true;
}

bool VRHeadset::getEnvBlendModes() {
  uint32_t envBlendModeCount = 0;
  XrResult result = xrEnumerateEnvironmentBlendModes(mXRInstance, mSystemID, mViewConfiguration, 0, &envBlendModeCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate blend modes (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Found %i blend modes\n", __FUNCTION__, envBlendModeCount);
  
  mEnvironmentBlendModes.resize(envBlendModeCount);
  result = xrEnumerateEnvironmentBlendModes(mXRInstance, mSystemID, mViewConfiguration, envBlendModeCount, &envBlendModeCount, mEnvironmentBlendModes.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate blend modes (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  for (const XrEnvironmentBlendMode &environmentBlendMode : mApplicationEnvironmentBlendModes) {
    if (std::find(mEnvironmentBlendModes.begin(), mEnvironmentBlendModes.end(), environmentBlendMode) != mEnvironmentBlendModes.end()) {
      mEnvironmentBlendMode = environmentBlendMode;
      break;
    }
  }
  if (mEnvironmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM) {
    Logger::log(1, "%s warning: Failed to find a compatible blend mode. Defaulting to XR_ENVIRONMENT_BLEND_MODE_OPAQUE.", __FUNCTION__);
    mEnvironmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  }

  Logger::log(1, "%s: Using blend mode %i\n", __FUNCTION__, mEnvironmentBlendMode);

  return true;
}

bool VRHeadset::getVulkanGraphicsRequirements() {
  PFN_xrGetVulkanGraphicsRequirementsKHR xrGetVulkanGraphicsRequirementsKHR = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrGetVulkanGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&xrGetVulkanGraphicsRequirementsKHR);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrGetVulkanGraphicsRequirementsKHR (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  result = xrGetVulkanGraphicsRequirementsKHR(mXRInstance, mSystemID, &mGraphicsRequirements);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get Vulkan graphics requirements (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Minimum OpenXR API Version is %i.%i.%i\n", __FUNCTION__,
    XR_VERSION_MAJOR(mGraphicsRequirements.minApiVersionSupported),
    XR_VERSION_MINOR(mGraphicsRequirements.minApiVersionSupported),
    XR_VERSION_PATCH(mGraphicsRequirements.minApiVersionSupported));

  Logger::log(1, "%s: Maximum OpenXR API Version is %i.%i.%i\n", __FUNCTION__,
    XR_VERSION_MAJOR(mGraphicsRequirements.maxApiVersionSupported),
    XR_VERSION_MINOR(mGraphicsRequirements.maxApiVersionSupported),
    XR_VERSION_PATCH(mGraphicsRequirements.maxApiVersionSupported));

  return true;
}


bool VRHeadset::createXRSession(VkRenderData& renderData) {
  PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrGetVulkanGraphicsDeviceKHR", (PFN_xrVoidFunction *)&xrGetVulkanGraphicsDeviceKHR);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrGetVulkanGraphicsDeviceKHR (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  result = xrGetVulkanGraphicsDeviceKHR(mXRInstance, mSystemID, renderData.rdVkbInstance.instance, &mPhysicalDevice);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get OpenXC physical device (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  if (renderData.rdVkbPhysicalDevice.physical_device != mPhysicalDevice) {
    Logger::log(1, "%s warning: OpenXR physial device is different from Vulkan physical device\n", __FUNCTION__);
    renderData.rdVkbPhysicalDevice.physical_device = mPhysicalDevice;
  }

  XrGraphicsBindingVulkanKHR xRGraphicsBinding{};
  xRGraphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
  xRGraphicsBinding.instance = renderData.rdVkbInstance.instance;
  xRGraphicsBinding.physicalDevice = renderData.rdVkbPhysicalDevice.physical_device;
  xRGraphicsBinding.device = renderData.rdVkbDevice.device;

  std::vector<VkQueueFamilyProperties> queueFamilies = renderData.rdVkbPhysicalDevice.get_queue_families();

  int i = 0;
  for (const auto& queueFamily : queueFamilies) {
    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      xRGraphicsBinding.queueFamilyIndex = i;
      break;
    }

    ++i;
  }

  auto queueIndexRet = renderData.rdVkbDevice.get_queue_index(vkb::QueueType::graphics);
  if (queueIndexRet.has_value()) {
    xRGraphicsBinding.queueIndex = queueIndexRet.value();
  }

  Logger::log(1, "%s: Found graphics queue familiy at index %i, graphics queue at index %i\n", __FUNCTION__, xRGraphicsBinding.queueFamilyIndex, xRGraphicsBinding.queueIndex);


  XrSessionCreateInfo sessionInfo{};
  sessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
  sessionInfo.createFlags = 0;
  sessionInfo.systemId = mSystemID;
  sessionInfo.next = &xRGraphicsBinding;

  result = xrCreateSession(mXRInstance, &sessionInfo, &mSession);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create Session (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: OpenXR session created\n", __FUNCTION__);
  return true;
}

bool VRHeadset::createXRDebugMessenger() {
  XrDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo{};
  debugMessengerCreateInfo.type = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  debugMessengerCreateInfo.messageSeverities =
    XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
    XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
    XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  debugMessengerCreateInfo.messageTypes =
    XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
    XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
    XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
    XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
  debugMessengerCreateInfo.userCallback = handleXRErrors;
  debugMessengerCreateInfo.userData = nullptr;

  PFN_xrCreateDebugUtilsMessengerEXT xrCreateDebugUtilsMessengerEXT = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)&xrCreateDebugUtilsMessengerEXT);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrCreateDebugUtilsMessengerEXT (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  result = xrCreateDebugUtilsMessengerEXT(mXRInstance, &debugMessengerCreateInfo, &mDebugMessenger);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create debug messenger (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Debug utils manager created\n", __FUNCTION__);
  return true;
}

std::vector<std::string> VRHeadset::getVKDeviceExtensionsForXR() {
  return mVKDeviceExtensionsForXR;
}

std::vector<std::string> VRHeadset::getVKInstanceExtensionsForXR() {
  return mVKInstanceExtensionsForXR;
}

bool VRHeadset::getVKDeviceExtensions() {
  PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensionsKHR = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrGetVulkanDeviceExtensionsKHR", (PFN_xrVoidFunction *)&xrGetVulkanDeviceExtensionsKHR);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrGetVulkanDeviceExtensionsKHR\n",  __FUNCTION__, result);
    return false;
  }

  uint32_t extensionNamesSize = 0;
  result = xrGetVulkanDeviceExtensionsKHR(mXRInstance, mSystemID, 0, &extensionNamesSize, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get number of Vulkan device extensions\n",  __FUNCTION__, result);
    return false;
  }

  std::vector<char> extensionNames(extensionNamesSize);
  result = xrGetVulkanDeviceExtensionsKHR(mXRInstance, mSystemID, extensionNamesSize, &extensionNamesSize, extensionNames.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get Vulkan instance extensions\n",  __FUNCTION__, result);
    return false;
  }

  std::stringstream streamData(extensionNames.data());
  std::string extension;
  while (std::getline(streamData, extension, ' ')) {
    mVKDeviceExtensionsForXR.push_back(extension);
    Logger::log(1, "%s: Found Vulkan device extension %s\n", __FUNCTION__, extension.c_str());
  }

  return true;
}

bool VRHeadset::getVKInstanceExtension() {
  PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction *)&xrGetVulkanInstanceExtensionsKHR);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrGetVulkanInstanceExtensionsKHR\n",  __FUNCTION__, result);
    return false;
  }

  uint32_t extensionNamesSize = 0;
  result = xrGetVulkanInstanceExtensionsKHR(mXRInstance, mSystemID, 0, &extensionNamesSize, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get number of Vulkan instance extensions\n",  __FUNCTION__, result);
    return false;
  }

  std::vector<char> extensionNames(extensionNamesSize);
  result = xrGetVulkanInstanceExtensionsKHR(mXRInstance, mSystemID, extensionNamesSize, &extensionNamesSize, extensionNames.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get Vulkan instance extensions\n",  __FUNCTION__, result);
    return false;
  }

  std::stringstream streamData(extensionNames.data());
  std::string extension;
  while (std::getline(streamData, extension, ' ')) {
    mVKInstanceExtensionsForXR.push_back(extension);
    Logger::log(1, "%s: Found Vulkan instance extension %s\n", __FUNCTION__, extension.c_str());
  }

  return true;
}

bool VRHeadset::createXRReferenceSpace() {
  // Fill out an XrReferenceSpaceCreateInfo structure and create a reference XrSpace, specifying a Local space with an identity pose as the origin.
  XrReferenceSpaceCreateInfo referenceSpaceCI{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  referenceSpaceCI.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  referenceSpaceCI.poseInReferenceSpace = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};

  XrResult result = xrCreateReferenceSpace(mSession, &referenceSpaceCI, &mLocalSpace);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create reference space (error code: %i)\n", __FUNCTION__, result);
  }

  return true;
}

bool VRHeadset::createXRSwapchain(VkRenderData &renderData) {
  uint32_t formatCount = 0;
  XrResult result = xrEnumerateSwapchainFormats(mSession, 0, &formatCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate number of spwachain formats (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  std::vector<int64_t> formats(formatCount);
  result = xrEnumerateSwapchainFormats(mSession, formatCount, &formatCount, formats.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate spwachain formats (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  int64_t chosenFormat = formats.front();

  for (int64_t format : formats) {
    if (format == VK_FORMAT_R8G8B8A8_UNORM) {
      chosenFormat = format;
      break;
    }
  }

  mSwapchains.resize(mViewConfigurationViews.size());
  for (size_t i = 0; i < mViewConfigurationViews.size(); ++i) {
    uint32_t viewCount = static_cast<uint32_t>(mViewConfigurationViews.size());

    XrSwapchainCreateInfo swapchainCreateInfo{};
    swapchainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainCreateInfo.format = chosenFormat;
    swapchainCreateInfo.sampleCount = mViewConfigurationViews.at(i).recommendedSwapchainSampleCount;
    swapchainCreateInfo.width = mViewConfigurationViews.at(i).recommendedImageRectWidth;
    swapchainCreateInfo.height = mViewConfigurationViews.at(i).recommendedImageRectHeight;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.arraySize = viewCount;
    swapchainCreateInfo.mipCount = 1;

    result = xrCreateSwapchain(mSession, &swapchainCreateInfo, &mSwapchains.at(i).swapchain);
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s error: Failed to create spwachain (error code: %i)\n", __FUNCTION__, result);
      return false;
    }

    mSwapchains.at(i).format = static_cast<VkFormat>(chosenFormat);

    uint32_t swapchainImageCount = 0;
    result = xrEnumerateSwapchainImages(mSwapchains.at(i).swapchain, 0, &swapchainImageCount, nullptr);
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s error: Failed to get number of swapchain images (error code: %i)\n", __FUNCTION__, result);
    }

    Logger::log(1, "%s: OpenXR Swapchain %i has %i images\n", __FUNCTION__, i, swapchainImageCount);

    std::vector<XrSwapchainImageVulkanKHR> swapchainImages(swapchainImageCount, { .type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });

    result = xrEnumerateSwapchainImages(mSwapchains.at(i).swapchain, swapchainImageCount, &swapchainImageCount, (XrSwapchainImageBaseHeader*)swapchainImages.data());
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s error: Failed to get swapchain images (error code: %i)\n", __FUNCTION__, result);
      return false;
    }

    mSwapchains.at(i).swapchainImages = std::move(swapchainImages);

    for (uint32_t j = 0; j < swapchainImageCount; j++) {
      VkImageViewCreateInfo imageViewCI{};
      imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      imageViewCI.viewType =VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      imageViewCI.image = mSwapchains.at(i).swapchainImages.at(j).image;
      imageViewCI.format = mSwapchains.at(i).format;
      imageViewCI.subresourceRange.baseMipLevel = 0;
      imageViewCI.subresourceRange.levelCount = 1;
      imageViewCI.subresourceRange.baseArrayLayer = 0;
      imageViewCI.subresourceRange.layerCount = viewCount;
      imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

      VkImageView imageView;
      VkResult vkresult = vkCreateImageView(renderData.rdVkbDevice.device, &imageViewCI, nullptr, &imageView);
      if (vkresult != VK_SUCCESS) {
        Logger::log(1, "%s error: Failed to create image view (error code: %i)\n", __FUNCTION__, vkresult);
        return false;
      }

      mSwapchains.at(i).swapchainImageViews.push_back(imageView);
    }
  }

  Logger::log(1, "%s: OpenXR Swapchains created (%ix%i)\n", __FUNCTION__,
    mViewConfigurationViews.at(0).recommendedImageRectWidth,
    mViewConfigurationViews.at(0).recommendedImageRectHeight);
  return true;
}

bool VRHeadset::createXRPipeline(VkRenderData& renderData) {
  std::vector<VkFormat> swapchainCopyAttachmentFormats {
    mSwapchains.at(0).format,
  };

  std::string vertexShaderFile = "shader/xr_swapchain_copy.vert.spv";
  std::string fragmentShaderFile = "shader/xr_swapchain_copy.frag.spv";
  if (!XRCopyPipeline::init(renderData, swapchainCopyAttachmentFormats, renderData.rdSwapchainCopyPipelineLayout,
      mXRSwapchainCopyPipeline,
      vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init XR Swapchain Copy shader pipeline\n", __FUNCTION__);
    return false;
  }

  return true;
}

bool VRHeadset::beginXRFrame(VkRenderData &renderData) {
  mFrameState = { .type = XR_TYPE_FRAME_STATE };

  XrFrameWaitInfo frameWaitInfo{};
  frameWaitInfo.type = XR_TYPE_FRAME_WAIT_INFO;

  XrResult result = xrWaitFrame(mSession, &frameWaitInfo, &mFrameState);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s: Failed to wait for OpenXR frame (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  XrFrameBeginInfo frameBeginInfo{};
  frameBeginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;

  result = xrBeginFrame(mSession, &frameBeginInfo);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s: Failed to begin OpenXR frame (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  bool sessionActive = (mSessionState == XR_SESSION_STATE_SYNCHRONIZED || mSessionState == XR_SESSION_STATE_VISIBLE || mSessionState == XR_SESSION_STATE_FOCUSED);
  if (sessionActive && mFrameState.shouldRender) {
    mViews = std::vector<XrView>(mViewConfigurationViews.size(), { XR_TYPE_VIEW });

    XrViewState viewState{};
    viewState.type = XR_TYPE_VIEW_STATE;

    XrViewLocateInfo viewLocateInfo{};
    viewLocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
    viewLocateInfo.viewConfigurationType = mViewConfiguration;
    viewLocateInfo.displayTime = mFrameState.predictedDisplayTime;
    viewLocateInfo.space = mLocalSpace;

    XrResult result = xrLocateViews(mSession, &viewLocateInfo, &viewState, static_cast<uint32_t>(mViews.size()), &mViewCount, mViews.data());
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s: Failed to locate views (error code: %i)\n", __FUNCTION__, result);
      return false;
    }

    // Camera update
    renderData.rdXRPoseOrientation.x = mViews.at(0).pose.orientation.x;
    renderData.rdXRPoseOrientation.y = mViews.at(0).pose.orientation.y;
    renderData.rdXRPoseOrientation.z = mViews.at(0).pose.orientation.z;
    renderData.rdXRPoseOrientation.w = mViews.at(0).pose.orientation.w;

    renderData.rdXRPosePosition.x = mViews.at(0).pose.position.x;
    renderData.rdXRPosePosition.y = mViews.at(0).pose.position.y;
    renderData.rdXRPosePosition.z = mViews.at(0).pose.position.z;

  }

  return true;
}

bool VRHeadset::renderXRFrame(VkRenderData &renderData) {
  bool rendered = false;
  XRRenderLayerInfo renderLayerInfos;
  renderLayerInfos.predictedDisplayTime = mFrameState.predictedDisplayTime;

  bool sessionActive = (mSessionState == XR_SESSION_STATE_SYNCHRONIZED || mSessionState == XR_SESSION_STATE_VISIBLE || mSessionState == XR_SESSION_STATE_FOCUSED);
  if (sessionActive && mFrameState.shouldRender) {
    rendered = renderXRLayer(renderData, renderLayerInfos);
    if (rendered) {
      renderLayerInfos.layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&renderLayerInfos.layerProjection));
    }
  }

  XrFrameEndInfo frameEndInfo{};
  frameEndInfo.type = XR_TYPE_FRAME_END_INFO;
  frameEndInfo.displayTime = mFrameState.predictedDisplayTime;
  frameEndInfo.environmentBlendMode = mEnvironmentBlendMode;
  frameEndInfo.layerCount = static_cast<uint32_t>(renderLayerInfos.layers.size());
  frameEndInfo.layers = renderLayerInfos.layers.data();

  XrResult result = xrEndFrame(mSession, &frameEndInfo);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s: Failed to end OpenXR frame (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  return true;
}

bool VRHeadset::renderXRLayer(VkRenderData &renderData, XRRenderLayerInfo& renderLayerInfo) {
  renderLayerInfo.layerProjectionViews.resize(mViewCount, { .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW });
  for (uint32_t i = 0; i < mViewCount; i++) {
    uint32_t colorImageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{};
    acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;

    XrResult result = xrAcquireSwapchainImage(mSwapchains.at(i).swapchain, &acquireInfo, &colorImageIndex);
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s: Failed to acquire image from Swapchain (error code: %i)\n", __FUNCTION__, result);
      return false;
    }

    XrSwapchainImageWaitInfo waitInfo{};
    waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    waitInfo.timeout = XR_INFINITE_DURATION;

    result = xrWaitSwapchainImage(mSwapchains.at(i).swapchain, &waitInfo);
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s: Failed to wait for image from Swapchain (error code: %i)\n", __FUNCTION__, result);
      return false;
    }

    const uint32_t &width = mViewConfigurationViews.at(i).recommendedImageRectWidth;
    const uint32_t &height = mViewConfigurationViews.at(i).recommendedImageRectHeight;

    renderLayerInfo.layerProjectionViews.at(i).type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    renderLayerInfo.layerProjectionViews.at(i).pose = mViews.at(i).pose;
    renderLayerInfo.layerProjectionViews.at(i).fov = mViews.at(i).fov;
    renderLayerInfo.layerProjectionViews.at(i).subImage.swapchain = mSwapchains.at(i).swapchain;
    renderLayerInfo.layerProjectionViews.at(i).subImage.imageRect.offset.x = 0;
    renderLayerInfo.layerProjectionViews.at(i).subImage.imageRect.offset.y = 0;
    renderLayerInfo.layerProjectionViews.at(i).subImage.imageRect.extent.width = static_cast<int32_t>(width);
    renderLayerInfo.layerProjectionViews.at(i).subImage.imageRect.extent.height = static_cast<int32_t>(height);
    renderLayerInfo.layerProjectionViews.at(i).subImage.imageArrayIndex = i;  // Useful for multiview rendering.

    // Vulkan rendering here
    // copy to Swapchain
    VkClearValue colorClearValue;
    colorClearValue.color = { { 0.25f, 0.25f, 0.25f, 1.0f } };
    VkViewport swapchainCopyViewport{};
    swapchainCopyViewport.x = 0.0f;
    swapchainCopyViewport.y = 0.0f;
    swapchainCopyViewport.width = static_cast<float>(width);
    swapchainCopyViewport.height = static_cast<float>(height);
    swapchainCopyViewport.minDepth = 0.0f;
    swapchainCopyViewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = VkExtent2D{width, height};

    vkCmdSetViewport(renderData.rdCommandBuffers.at(renderData.currentFrame), 0, 1, &swapchainCopyViewport);
    vkCmdSetScissor(renderData.rdCommandBuffers.at(renderData.currentFrame), 0, 1, &scissor);

    VkRenderingAttachmentInfo swapchainAttachmentInfo {};
    swapchainAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    swapchainAttachmentInfo.imageView = mSwapchains.at(i).swapchainImageViews.at(colorImageIndex);
    swapchainAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapchainAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    swapchainAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    swapchainAttachmentInfo.clearValue = colorClearValue;

    std::vector<VkRenderingAttachmentInfo> swapchainCopyAttachmentInfos {
      swapchainAttachmentInfo,
    };

    VkRect2D swapchainRenderArea = VkRect2D{VkOffset2D{}, VkExtent2D{width, height}};

    VkRenderingInfo swapchainCopyRenderInfo{};
    swapchainCopyRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    swapchainCopyRenderInfo.renderArea = swapchainRenderArea;
    swapchainCopyRenderInfo.layerCount = 2;
    swapchainCopyRenderInfo.viewMask = 0b00000011;
    swapchainCopyRenderInfo.colorAttachmentCount = static_cast<uint32_t>(swapchainCopyAttachmentInfos.size());
    swapchainCopyRenderInfo.pColorAttachments = swapchainCopyAttachmentInfos.data();

    vkCmdBeginRendering(renderData.rdCommandBuffers.at(renderData.currentFrame), &swapchainCopyRenderInfo);

    vkCmdBindPipeline(renderData.rdCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mXRSwapchainCopyPipeline);

    vkCmdBindDescriptorSets(renderData.rdCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.rdSwapchainCopyPipelineLayout, 0, 1,
      &renderData.rdSwapchainCopyDescriptorSets.at(renderData.currentFrame), 0, nullptr);

    vkCmdDraw(renderData.rdCommandBuffers.at(renderData.currentFrame), 3, 1, 0, 0);

    vkCmdEndRendering(renderData.rdCommandBuffers.at(renderData.currentFrame));

    // Vulkan rendering end

    XrSwapchainImageReleaseInfo releaseInfo{};
    releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    result = xrReleaseSwapchainImage(mSwapchains.at(i).swapchain, &releaseInfo);
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s: Failed to release image back to Swapchain (error code: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  renderLayerInfo.layerProjection.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
  renderLayerInfo.layerProjection.space = mLocalSpace;
  renderLayerInfo.layerProjection.viewCount = static_cast<uint32_t>(renderLayerInfo.layerProjectionViews.size());
  renderLayerInfo.layerProjection.views = renderLayerInfo.layerProjectionViews.data();

  return true;
}

void VRHeadset::destroyXRInstance() {
  XrResult result = xrDestroyInstance(mXRInstance);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to destroy Instance (error code: %i)\n", __FUNCTION__, result);
  }
}

void VRHeadset::destroyXRSession() {
  XrResult result = xrDestroySession(mSession);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to destroy Session (error code: %i)\n", __FUNCTION__, result);
  }
}

void VRHeadset::destroyXRDebugMessenger() {
  PFN_xrDestroyDebugUtilsMessengerEXT xrDestroyDebugUtilsMessengerEXT = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)&xrDestroyDebugUtilsMessengerEXT);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrDestroyDebugUtilsMessengerEXT (error code: %i)\n", __FUNCTION__, result);
  }

  result = xrDestroyDebugUtilsMessengerEXT(mDebugMessenger);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to destroy debug messenger (error code: %i)\n", __FUNCTION__, result);
  }
}

void VRHeadset::destroyXRReferenceSpace() {
  XrResult result = xrDestroySpace(mLocalSpace);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to destroy reference space (error code: %i)\n", __FUNCTION__, result);
  }
}

void VRHeadset::destroyXRSwapchain(VkRenderData &renderData) {
  for (size_t i = 0; i < mViewConfigurationViews.size(); ++i) {
    for (VkImageView &imageView : mSwapchains.at(i).swapchainImageViews) {
      vkDestroyImageView(renderData.rdVkbDevice.device, imageView, nullptr);
    }

    XrResult result = xrDestroySwapchain(mSwapchains.at(i).swapchain);
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s error: Failed to destroy swapchain (error code: %i)\n", __FUNCTION__, result);
    }
  }
}

void VRHeadset::destroyXRPipeline(VkRenderData& renderData) {
  XRCopyPipeline::cleanup(renderData, mXRSwapchainCopyPipeline);
}

XrBool32 VRHeadset::handleXRErrors(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT type,
    const XrDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData) {

  std::string severityString;
  std::string typeString;

  switch (type) {
      case XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT :
        typeString = "general";
        break;
      case XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT :
        typeString = "validation";
        break;
      case XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT :
        typeString = "performance";
        break;
      case XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT :
        typeString = "conformance";
        break;
    }

    switch (severity){
      case XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT :
        severityString = "verbose";
        break;
      case XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT :
        severityString = "info";
        break;
      case XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT :
        severityString = "warning";
        break;
      case XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT :
        severityString = "error";
        break;
    }

    Logger::log(1, "%s: %s %s - '%s'\n", __FUNCTION__, typeString.c_str(), severityString.c_str(), callbackData->message);

    return XR_FALSE;
}
