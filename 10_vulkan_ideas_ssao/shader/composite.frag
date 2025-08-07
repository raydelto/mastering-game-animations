#version 460 core

layout (input_attachment_index = 0, set = 0, binding = 1) uniform subpassInput inputColor;
layout (input_attachment_index = 1, set = 0, binding = 2) uniform subpassInput inputDepth;
layout (input_attachment_index = 2, set = 0, binding = 3) uniform subpassInput inputNormal;

layout (set = 0, binding = 5) uniform sampler2D ssao;
layout (set = 0, binding = 6) uniform sampler2D ssaoBlur;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

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

float toSRGB(float x) {
  if (x <= 0.0031308)
    return 12.92 * x;
  else
    return 1.055 * pow(x, (1.0/2.4)) - 0.055;
}
vec3 sRGB(vec3 c) {
  return vec3(toSRGB(c.x), toSRGB(c.y), toSRGB(c.z));
}

vec3 getPosFromDepth(vec2 uv) {
  float depth = subpassLoad(inputDepth).r;
  vec2 xy = uv * 2.0 - 1.0;
  vec4 pos = vec4(xy, depth, 1.0);
  pos = invProjectionMat * pos;
  pos.xyz /= pos.w;

  return pos.xyz;
}

float linearDepth(float depth) {
  return 2.0 * nearPlane / (farPlane + nearPlane - depth * (farPlane - nearPlane));
}

void main() {
  // Read G-Buffer values from previous sub pass
  float fragDepth = getPosFromDepth(inUV).z;
  vec3 normal = normalize(subpassLoad(inputNormal).rgb * 2.0 - 1.0);
  vec3 albedo = subpassLoad(inputColor).rgb;

  float ao = texture(ssao, inUV).r;
  float aoBlur = texture(ssaoBlur, inUV).r;

  float ssaoValue = (ssaoBlurEnabled == 1) ? aoBlur : ao;

  float ambientStrength = 0.1;
  vec3 ambient = ambientStrength * max(vec3(lightColor), vec3(0.05, 0.05, 0.05)) * albedo.rgb;

  vec3 lightDir = normalize(vec3(lightPos));
  float diff = max(dot(normalize(normal), normalize(vec3(lightDir))), 0.0);
  vec3 diffuse = diff * vec3(lightColor) * albedo.rgb;

  float fogAmount = 1.0 - clamp(exp(-pow(fogDensity * fragDepth, 2.0)), 0.0, 1.0);
  vec4 fogColor = 0.25 * vec4(vec3(lightColor), 1.0);

  switch (compositeDebug) {
    case 0:
      outColor = mix(vec4(ambient + diffuse * ssaoValue, 1.0), fogColor, fogAmount);
      outColor.rgb = sRGB(outColor.rgb);
      break;
    case 1:
      outColor = vec4(albedo, 1.0);
      break;
    case 2:
      if (farPlane == 0.0) {
        outColor = vec4(vec3(subpassLoad(inputDepth).r), 1.0);
      } else {
        outColor = vec4(vec3(linearDepth(subpassLoad(inputDepth).r)), 1.0);
      }
      break;
    case 3:
      outColor = vec4(normal * 0.5 + 0.5, 1.0);
      break;
    case 4:
      outColor = vec4(vec3(ao), 1.0);
      break;
    case 5:
      outColor = vec4(vec3(aoBlur), 1.0);
      break;
  }
}
