#version 460 core

layout (set = 0, binding = 0) uniform sampler2DArray inputColor;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

void main() {
  float layer = 0.0;
  vec2 uv = inUV;
  uv.x *= 2.0;
  if (inUV.x > 0.5) {
    layer = 1.0;
    uv.x = (inUV.x - 0.5) * 2.0;
  }

  vec3 albedo = texture(inputColor, vec3(uv, layer)).rgb;
  outColor = vec4(albedo, 1.0);
}
