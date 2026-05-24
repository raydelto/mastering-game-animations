#version 460 core
#extension GL_EXT_multiview : enable

layout (location = 0) in vec4 aPos;

layout (location = 0) out vec3 texCoord;

layout (std140, set = 1, binding = 0) uniform Matrices {
  mat4 viewMat[2];
  mat4 projectionMat[2];
  mat4 invViewMat[2];
  mat4 invProjectionMat[2];
};

void main() {
  /* remove translation part from inverse view matrix */
  texCoord = mat3(invViewMat[gl_ViewIndex]) * (invProjectionMat[gl_ViewIndex] * aPos).xyz;

  /* set z to 1.0 to force drawing on the far z plane */
  gl_Position = aPos.xyww;
}
