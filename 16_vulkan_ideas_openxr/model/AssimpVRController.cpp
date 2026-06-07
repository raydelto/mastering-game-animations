#include <AssimpVRController.h>

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Logger.h>

AssimpVRController::AssimpVRController(std::shared_ptr<AssimpModel> model, glm::mat4 transformMatrix) : mAssimpModel(model) {
  if (!model) {
    Logger::log(1, "%s error: invalid model given\n", __FUNCTION__);
    return;
  }

  mAssimpModel = model;

  mLocalTransformMatrix = transformMatrix;

  // save model root matrix
  mModelRootMatrix = mAssimpModel->getRootTranformationMatrix();

  updateModelRootMatrix();
}

std::shared_ptr<AssimpModel> AssimpVRController::getModel() {
  return mAssimpModel;
}

void AssimpVRController::updateModelRootMatrix() {
  mInstanceRootMatrix =  mLocalTransformMatrix * mModelRootMatrix;
}

void AssimpVRController::setLocalTransformMatrix(glm::mat4 transformMatrix) {
  mLocalTransformMatrix = transformMatrix;
  updateModelRootMatrix();
}

glm::mat4 AssimpVRController::getWorldTransformMatrix() {
  return mInstanceRootMatrix;
}
