#version 460 core
layout (location = 0) in vec2 inUV;

layout (location = 5) out float outColor;

layout (set = 0, binding = 2) uniform sampler2D inputDepth;
layout (set = 0, binding = 3) uniform sampler2D inputNormal;
layout (set = 0, binding = 4) uniform sampler2D ssaoNoise;

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
};

layout (std140, set = 0, binding = 1) uniform SSAOSettings {
  float ssaoRadius;
  float ssaoBias;
  int ssaoExponent;
  int ssaoBlurRadius;
};

layout (constant_id = 0) const int SSAO_KERNEL_SIZE = 64;

layout (std140, set = 0, binding = 5) uniform SSAOSamples {
  vec4 samples[SSAO_KERNEL_SIZE];
};

vec3 getPosFromDepth(vec2 uv) {
  float depth = texture(inputDepth, uv).r;
  vec2 xy = uv * 2.0 - 1.0;
  vec4 pos = vec4(xy, depth, 1.0);
  pos = invProjectionMat * pos;
  pos.xyz /= pos.w;

  return pos.xyz;
}

void main() {
  vec3 fragPos = getPosFromDepth(inUV);
  // set w to zero to nullify translation
  vec3 normal = (viewMat * vec4(normalize(texture(inputNormal, inUV).rgb * 2.0 - 1.0), 0.0)).xyz;

  ivec2 texDim = textureSize(inputDepth, 0);
  ivec2 noiseDim = textureSize(ssaoNoise, 0);
  const float scalingFactor = 0.5;
  const vec2 noiseUV = vec2(float(texDim.x)/float(noiseDim.x),
                            float(texDim.y)/(noiseDim.y)) * inUV * scalingFactor;
  vec3 randomVec = texture(ssaoNoise, noiseUV).xyz;

  vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
  vec3 bitangent = cross(tangent, normal);
  mat3 TBN = mat3(tangent, bitangent, normal);

  float occlusion = 0.0;
  for (int i = 0; i < SSAO_KERNEL_SIZE; i++) {
    vec3 samplePos = TBN * samples[i].xyz;
    samplePos = fragPos + samplePos * ssaoRadius;

    vec4 offset = vec4(samplePos, 1.0);
    offset = projectionMat * offset;
    offset.xy /= offset.w;
    offset.xy = offset.xy * 0.5 + 0.5;

    float sampleDepth = getPosFromDepth(offset.xy).z;
    vec3 sampleNormal = (viewMat * vec4(normalize(texture(inputNormal,offset.xy).rgb * 2.0 - 1.0), 0.0)).xyz;

    if (dot(sampleNormal, normal) < 0.99) {
      float rangeCheck = smoothstep(0.0, 1.0, ssaoRadius / abs(fragPos.z - sampleDepth));
      occlusion += (sampleDepth >= samplePos.z + ssaoBias ? 1.0 : 0.0) * rangeCheck;
    }
  }

  occlusion = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));

  outColor = pow(occlusion, ssaoExponent);
}
