#pragma once

#include <string>
#include <glm/glm.hpp>

#include <Enums.h>

struct DynamicLightSettings {
  glm::vec3 dlsWorldPosition = glm::vec3(0.0f);
  glm::vec3 dlsWorldRotation = glm::vec3(0.0f);

  int dlsIndexPosition = -1;

  bool dlsLightEnabled = false;
  bool dlsLightDebugEnabled = false;

  dynamicLightType dlsLightType = dynamicLightType::point;
  glm::vec3 dlsDiffuseColor = glm::vec3(1.0f);

  // constant, linear, quadratic attenuations:
  // constant = 1.0
  // linear ~ 1.0 / distance / 2.2
  // quadratic ~ 1.0 / distance^2 * 7.5
  float dlsLightDistance = 10.0f;
  float dlsMaxLightDistance = 12.0f;

  float dlsPointCutOffDegrees = 12.5f;
  float dlsPointOuterCutOffDegrees = 17.5f;
};
