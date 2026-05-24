#include <cstring>
#include <algorithm>

#include <VRHeadset.h>

#include <Logger.h>


bool VRHeadset::init(void* bindings) {
  Logger::log(1, "%s: VR Headset init start\n", __FUNCTION__);

  if (!createInstance()) {
    return false;
  }

  if (!createDebugMessenger()) {
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

  if (!createSession(bindings)) {
    return false;
  }

  Logger::log(1, "%s: VR Headset init success\n", __FUNCTION__);

  return true;
}

void VRHeadset::cleanup() {
  destroySession();
  destroyDebugMessenger();
  destroyInstance();
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
            Logger::log(1, "%s warning: Found unhandled session change state\n", __FUNCTION__);
            break;
        }

        // Store state for use in the application
        mSessionState = sessionStateChanged->state;
        break;
      }
      default:
        Logger::log(1, "%s warning: Found unhandled event\n", __FUNCTION__);
        break;
    }
  }
}

bool VRHeadset::createInstance() {
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
  mInstanceExtensions.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);

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


bool VRHeadset::createSession(void* bindings) {
  XrSessionCreateInfo sessionInfo{};
  sessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
  sessionInfo.createFlags = 0;
  sessionInfo.systemId = mSystemID;
  sessionInfo.next = bindings;

  XrResult result = xrCreateSession(mXRInstance, &sessionInfo, &mSession);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create Session (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  return true;
}

bool VRHeadset::createDebugMessenger() {
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

void VRHeadset::destroyInstance() {
  XrResult result = xrDestroyInstance(mXRInstance);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to destroy Instance (error code: %i)\n", __FUNCTION__, result);
  }
}

void VRHeadset::destroySession() {
  XrResult result = xrDestroySession(mSession);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to destroy Session (error code: %i)\n", __FUNCTION__, result);
  }
}

void VRHeadset::destroyDebugMessenger() {
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
