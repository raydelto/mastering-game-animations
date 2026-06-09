#include <AppWindow.h>
#include <Logger.h>
#include <ModelInstanceCamCallbacks.h>
#include <VkRenderer.h>

bool AppWindow::init(unsigned int width, unsigned int height, std::string title) {
  if (!glfwInit()) {
    Logger::log(1, "%s: glfwInit() error\n", __FUNCTION__);
    return false;
  }

  if (!glfwVulkanSupported()) {
    glfwTerminate();
    Logger::log(1, "%s error: Vulkan is not supported\n", __FUNCTION__);
    return false;
  }

  // Vulkan needs no context
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  mWindowTitle = title;
  mWindow = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

  if (!mWindow) {
    glfwTerminate();
    Logger::log(1, "%s error: Could not create window\n", __FUNCTION__);
    return false;
  }

  // allow to set window title in renderer
  ModelInstanceCamCallbacks rendererMICCallbacks;
  rendererMICCallbacks.micGetWindowTitleFunction = [this]() { return getWindowTitle(); };
  rendererMICCallbacks.micSetWindowTitleFunction = [this](std::string windowTitle) { setWindowTitle(windowTitle); };

  rendererMICCallbacks.micIsAudioManagerInitializedCallbackFunction = [this]() { return mAudioManager.isInitialized(); };
  rendererMICCallbacks.micPlayRandomMusicCallbackFunction = [this]() { mAudioManager.playRandomMusic(); };
  rendererMICCallbacks.micStopMusicCallbackFunction = [this]() { mAudioManager.stopMusic(); };
  rendererMICCallbacks.micPauseResumeMusicCallbackFunction = [this](bool pauseOrResume) { mAudioManager.pauseMusic(pauseOrResume); };
  rendererMICCallbacks.micGetMusicPlayListCallbackFunction = [this]() { return mAudioManager.getPlayList(); };
  rendererMICCallbacks.micIsMusicPausedCallbackFunction = [this]() { return mAudioManager.isMusicPaused(); };
  rendererMICCallbacks.micIsMusicPlayingCallbackFunction = [this]() { return mAudioManager.isMusicPlaying(); };
  rendererMICCallbacks.micGetMusicCurrentTrackCallbackFunction = [this]() { return mAudioManager.getCurrentTitle(); };
  rendererMICCallbacks.micPlayNextMusicTrackCallbackFunction = [this]() { mAudioManager.playNextTitle(); };
  rendererMICCallbacks.micPlayPrevMusicTrackCallbackFunction = [this]() { mAudioManager.playPrevTitle(); };
  rendererMICCallbacks.micSetMusicVolumeCallbackFunction = [this](int volume) { mAudioManager.setMusicVolume(volume); };
  rendererMICCallbacks.micGetMusicVolumeCallbackFunction = [this]() { return mAudioManager.getMusicVolume(); };
  rendererMICCallbacks.micPlayMusicTitleCallbackFunction = [this](std::string title) { mAudioManager.playTitle(title); };

  rendererMICCallbacks.micSetSoundEffectsVolumeCallbackFunction = [this](int volume) { mAudioManager.setSoundVolume(volume); };
  rendererMICCallbacks.micGetSoundEffectsVolumeCallbackFunction = [this]() { return mAudioManager.getSoundVolume(); };
  rendererMICCallbacks.micPlayWalkFootstepCallbackFunction = [this]() { mAudioManager.playWalkFootsteps(); };
  rendererMICCallbacks.micPlayRunFootstepCallbackFunction = [this]() { mAudioManager.playRunFootsteps(); };
  rendererMICCallbacks.micStopFootstepCallbackFunction = [this]() { mAudioManager.stopFootsteps(); };

  if (!mVRHeadset.init(mWindow, rendererMICCallbacks)) {
    glfwTerminate();
    Logger::log(1, "%s error: Could not init VR Headset\n", __FUNCTION__);
    return false;
  }

  std::shared_ptr<VkRenderer> renderer = mVRHeadset.getVulkanRenderer();

  glfwSetWindowUserPointer(mWindow, renderer.get());
  glfwSetFramebufferSizeCallback(mWindow, [](GLFWwindow *win, int width, int height) {
      auto renderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
      renderer->setSize(width, height);
    }
  );

  glfwSetKeyCallback(mWindow, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
      auto renderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
      renderer->handleKeyEvents(key, scancode, action, mods);
    }
  );

  glfwSetMouseButtonCallback(mWindow, [](GLFWwindow *win, int button, int action, int mods) {
      auto renderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
      renderer->handleMouseButtonEvents(button, action, mods);
    }
  );

  glfwSetCursorPosCallback(mWindow, [](GLFWwindow *win, double xpos, double ypos) {
      auto renderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
      renderer->handleMousePositionEvents(xpos, ypos);
    }
  );

  glfwSetScrollCallback(mWindow, [](GLFWwindow *win, double xOffset, double yOffset) {
    auto renderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
    renderer->handleMouseWheelEvents(xOffset, yOffset);
    }
  );

  glfwSetWindowCloseCallback(mWindow, [](GLFWwindow *win) {
      auto renderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
      renderer->requestExitApplication();
    }
  );

  // use SDL for audio
  if (!mAudioManager.init()) {
    Logger::log(1, "%s error: unable to init audio, skipping\n", __FUNCTION__);
  }

  if (mAudioManager.isInitialized()) {
    if (!mAudioManager.loadMusicFromFolder("assets/music", "mp3")) {
      Logger::log(1, "%s warning: not MP3 tracks found, skipping\n", __FUNCTION__);
    }
    if (!mAudioManager.loadMusicFromFolder("assets/music", "ogg")) {
      Logger::log(1, "%s warning: not OGG tracks found, skipping\n", __FUNCTION__);
    }
    if (!mAudioManager.loadWalkFootsteps("assets/sounds/Fantozzi-SandL1.wav")) {
      Logger::log(1, "%s warning: could not load walk footsteps, skipping\n", __FUNCTION__);
    }
    if (!mAudioManager.loadRunFootsteps("assets/sounds/Fantozzi-SandR3.wav")) {
      Logger::log(1, "%s warning: could not load run footsteps, skipping\n", __FUNCTION__);
    }
  }
  Logger::log(1, "%s: Window with Vulkan successfully initialized\n", __FUNCTION__);

  return true;
}

void AppWindow::mainLoop() {
  // Disable VSYNC for the mirror window to prevent double-blocking with xrWaitFrame
  glfwSwapInterval(0);

  std::chrono::time_point<std::chrono::steady_clock> loopStartTime = std::chrono::steady_clock::now();
  std::chrono::time_point<std::chrono::steady_clock> loopEndTime = std::chrono::steady_clock::now();
  float deltaTime = 0.0f;

  while (mVRHeadset.isXRApplicationRunning() && !glfwWindowShouldClose(mWindow)) {
    mVRHeadset.pollEvents();

    if (mVRHeadset.isXRSessionRunning()) {
      if (!mVRHeadset.draw(deltaTime)) {
        break;
      }
    }

    glfwPollEvents();

    // calculate the time we needed for the current frame, feed it to the next draw() call
    loopEndTime =  std::chrono::steady_clock::now();

    // delta time in seconds
    deltaTime = std::chrono::duration_cast<std::chrono::microseconds>(loopEndTime - loopStartTime).count() / 1'000'000.0f;
    loopStartTime = loopEndTime;
  }
}

void AppWindow::cleanup() {
  mAudioManager.cleanup();
  mVRHeadset.cleanup();

  glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
  glfwDestroyWindow(mWindow);
  glfwTerminate();
  Logger::log(1, "%s: Terminating Window\n", __FUNCTION__);
}

std::string AppWindow::getWindowTitle() {
  return mWindowTitle;
}

void AppWindow::setWindowTitle(std::string newTitle) {
  mWindowTitle = newTitle;
  glfwSetWindowTitle(mWindow, mWindowTitle.c_str());
}
