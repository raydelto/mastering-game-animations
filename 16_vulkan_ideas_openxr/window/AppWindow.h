#pragma once
#include <string>
#include <memory>
#include <chrono>
#include <GLFW/glfw3.h>

#include <VRHeadset.h>
#include <AudioManager.h>
#include <Callbacks.h>

class AppWindow {
  public:
    bool init(unsigned int width, unsigned int height, std::string title);
    void mainLoop();
    void cleanup();

    GLFWwindow* getWindow();
    std::string getWindowTitle();
    void setWindowTitle(std::string newTitle);

  private:
    GLFWwindow *mWindow = nullptr;

    std::string mWindowTitle;

    VRHeadset mVRHeadset{};

    AudioManager mAudioManager{};
};
