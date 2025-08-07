#version 460 core
layout (location = 0) in vec3 color;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 texCoord;
layout (location = 3) flat in float selectInfo;

layout (location = 1) out vec4 FragColor;
layout (location = 2) out float outDepth;
layout (location = 3) out vec4 outNormal;
layout (location = 4) out float SelectedInstance;

layout (set = 0, binding = 0) uniform sampler2D tex;

layout (std140, set = 1, binding = 0) uniform Matrices {
  mat4 viewMat;
  mat4 projectionMat;
  mat4 invViewMat;
  mat4 invPprojectionMat;
  vec4 cameraPos;
  vec4 lightPos;
  vec4 lightColor;
  float nearPlane;
  float farPlane;
  float fogDensity;
};

void main() {
  FragColor = texture(tex, texCoord) * vec4(color, 1.0);

  outDepth = gl_FragCoord.z;
  outNormal = vec4(normalize(normal) * 0.5 + 0.5, 1.0);

  SelectedInstance = selectInfo;
}
