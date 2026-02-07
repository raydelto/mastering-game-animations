#version 460 core
layout (location = 0) in vec2 inUV;

layout (location = 0) out float outColor;

layout (set = 0, binding = 2) uniform sampler2D blurInputDepth;
layout (set = 0, binding = 3) uniform sampler2D blurInputNormal;
layout (set = 0, binding = 4) uniform sampler2D ssaoColor;

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
  float fogDensity;
  int compositeDebug;
  int ssaoBlurEnabled;
};

layout (std140, set = 0, binding = 1) uniform SSAOSettings {
  float ssaoRadius;
  float ssaoBias;
  int ssaoExponent;
  int ssaoBlurRadius;
};

float unlinearizeDepth(float depth) {
  return -(2.0 * nearPlane / depth - farPlane + nearPlane)/  (farPlane - nearPlane);
}

vec3 getWorldPosFromDepth(vec2 uv) {
  float depth = 1.0;
  if (farPlane == 0.0) {
    depth = texture(blurInputDepth, uv).r;
  } else {
    depth = unlinearizeDepth(texture(blurInputDepth, uv).r);
  }
  vec2 xy = uv * 2.0 - 1.0;
  vec4 pos = vec4(xy, depth, 1.0);
  pos = invProjectionMat * pos;
  pos.xyz /= pos.w;

  return pos.xyz;
}

void main() {
  /* just copy the pixels */
  if (ssaoBlurEnabled == 0) {
    outColor = texture(ssaoColor, inUV).r;
    return;
  }

  vec2 texelSize = 1.0 / vec2(textureSize(ssaoColor, 0));

  float depth = getWorldPosFromDepth(inUV).z;
  vec3 normal = (viewMat * vec4(normalize(texture(blurInputNormal, inUV).rgb * 2.0 - 1.0), 0.0)).xyz;

  float result = 0.0;
  int sampleCount = 0;
  for (int x = -ssaoBlurRadius; x <= ssaoBlurRadius; x++) {
    for (int y = -ssaoBlurRadius; y <= ssaoBlurRadius; y++)  {
      vec2 offset = vec2(float(x), float(y)) * texelSize;

      float offsetDepth = getWorldPosFromDepth(inUV + offset).z;
      vec3 offsetNormal = (viewMat * vec4(normalize(texture(blurInputNormal, inUV + offset).rgb * 2.0 - 1.0), 0.0)).xyz;

      /* sharper edges */
      if (abs(depth - offsetDepth) < 0.2 && dot(normal, offsetNormal) > 0.85) {
        result += texture(ssaoColor, inUV + offset).r;
        ++sampleCount;
      }
    }
  }
  outColor = clamp(result / (float(sampleCount)), 0.0, 1.0);
}
