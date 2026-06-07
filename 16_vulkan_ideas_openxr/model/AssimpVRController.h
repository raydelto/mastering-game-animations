#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include <AssimpModel.h>

class AssimpVRController {
  public:
    AssimpVRController(std::shared_ptr<AssimpModel> model, glm::mat4 transformMatrix = glm::mat4(1.0f));

    void draw(VkRenderData &renderData);
    std::shared_ptr<AssimpModel> getModel();
    glm::mat4 getWorldTransformMatrix();
    void setLocalTransformMatrix(glm::mat4 transformMatrix);

private:
    void updateModelRootMatrix();

    std::shared_ptr<AssimpModel> mAssimpModel = nullptr;

    glm::mat4 mLocalTransformMatrix = glm::mat4(1.0f);
    glm::mat4 mInstanceRootMatrix = glm::mat4(1.0f);
    glm::mat4 mModelRootMatrix = glm::mat4(1.0f);
};
