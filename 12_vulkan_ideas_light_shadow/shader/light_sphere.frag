#version 460 core
layout (location = 0) in vec2 inUV;
layout (location = 1) in flat uint inInstance;

layout (location = 7) out vec4 FragColor;

layout (input_attachment_index = 0, set = 0, binding = 2) uniform subpassInput inputDepth;
layout (input_attachment_index = 2, set = 0, binding = 3) uniform subpassInput inputNormal;

layout (std140, set = 0, binding = 0) uniform Matrices {
  mat4 viewMat;
  mat4 projectionMat;
  mat4 invViewMat;
  mat4 invProjectionMat;
  vec4 cameraPos;
  vec4 lightPos;
  vec4 lightColor;
  float nearPlane;
  float farPlane;
};

struct dynamicLight {
  vec4 position;
  vec4 rotation;
  vec4 color;
  float distance;
  float maxDistance;
  uint type;
  float cutOff;
  float outerCutOff;
  float constantAttFactor;
  float linearAttFactor;
  float quadraticAttFactor;
};

layout (std430, set = 0, binding = 1) readonly restrict buffer DynamicLights {
  dynamicLight lights[];
};

float unlinearizeDepth(float depth) {
  return -(2.0 * nearPlane / depth - farPlane - nearPlane) / (farPlane - nearPlane);
}

vec3 getWorldPosFromDepth(vec2 uv) {
  float depth = 0.0;
  if (farPlane == 0.0) {
    depth = subpassLoad(inputDepth).r;
  } else {
    depth = unlinearizeDepth(subpassLoad(inputDepth).r);
  }
  vec2 xy = uv * 2.0 - 1.0;
  vec4 pos = vec4(xy, depth, 1.0);
  pos = invProjectionMat * pos;
  pos.xyz /= pos.w;

  return pos.xyz;
}

void main() {
  vec3 lightDynDiff = vec3(0.0);

  vec3 viewPos = getWorldPosFromDepth(inUV);
  vec3 worldPos = vec3(invViewMat * vec4(getWorldPosFromDepth(inUV), 1.0));
  vec3 normal = normalize(subpassLoad(inputNormal).rgb * 2.0 - 1.0);

  vec3 lightPos = lights[inInstance].position.xyz / lights[inInstance].position.w;
  vec3 lightDir = normalize(lightPos - worldPos);
  float diff = max(dot(normal, lightDir), 0.0);

  float distance = length(lightPos - worldPos);
  if (distance < lights[inInstance].maxDistance) {
    float attenuation = 1.0 / (lights[inInstance].constantAttFactor + lights[inInstance].linearAttFactor * distance + lights[inInstance].quadraticAttFactor * (distance * distance));

    lightDynDiff = vec3(lights[inInstance].color) * diff * attenuation;
    if (lights[inInstance].type == 1) {
      float theta = dot(lightDir, normalize(-vec3(lights[inInstance].rotation)));
      float epsilon = lights[inInstance].cutOff - lights[inInstance].outerCutOff;

      float intensity = clamp((theta - lights[inInstance].outerCutOff) / epsilon, 0.0, 1.0);

      lightDynDiff *= intensity;
    }
  }

  FragColor = vec4(lightDynDiff, 1.0);
}
