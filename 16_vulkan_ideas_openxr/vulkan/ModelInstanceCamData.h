// separate settings file to avoid cicrula dependecies
#pragma once

#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>

#include <Callbacks.h>
#include <Enums.h>

// forward declaration
class AssimpModel;
class AssimpInstance;
class AssimpSettingsContainer;
class AssimpLevel;
class AssimpDynLight;
class Camera;
class SingleInstanceBehavior;

struct ModelInstanceCamData {
  std::vector<std::shared_ptr<AssimpModel>> micModelList{};
  int micSelectedModel = 0;

  std::vector<std::shared_ptr<AssimpInstance>> micAssimpInstances{};
  std::unordered_map<std::string, std::vector<std::shared_ptr<AssimpInstance>>> micAssimpInstancesPerModel{};
  int micSelectedInstance = 0;

  std::shared_ptr<AssimpSettingsContainer> micSettingsContainer{};

  std::vector<std::shared_ptr<Camera>> micCameras{};
  int micSelectedCamera = 0;

  std::unordered_map<cameraType, std::string> micCameraTypeMap{};
  std::unordered_map<moveDirection, std::string> micMoveDirectionMap{};
  std::unordered_map<moveState, std::string> micMoveStateMap{};

  std::set<std::pair<int, int>> micInstanceCollisions{};

  std::map<std::string, std::shared_ptr<SingleInstanceBehavior>> micBehaviorData{};
  std::unordered_map<nodeEvent, std::string> micNodeUpdateMap{};

  std::unordered_map<faceAnimation, std::string> micFaceAnimationNameMap{};
  std::unordered_map<headMoveDirection, std::string> micHeadMoveAnimationNameMap{};

  std::vector<std::shared_ptr<AssimpLevel>> micLevels{};
  int micSelectedLevel = 0;

  std::vector<std::shared_ptr<AssimpDynLight>> micDynLights{};
  int micSelectedDynLight = 0;

  /* we can only delete models and levels in Vulkan outside the command buffers,
   * so let's use a separate pending list */
  std::unordered_set<std::shared_ptr<AssimpModel>> micPendingDeleteAssimpModels{};
  std::unordered_set<std::shared_ptr<AssimpLevel>> micPendingDeleteAssimpLevels{};
};
