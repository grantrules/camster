#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(binding = 0) uniform UBO {
  mat4 model;
  mat4 view;
  mat4 projection;
  vec4 options;
} ubo;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragWorldPos;

void main() {
  vec4 worldPos = ubo.model * vec4(inPos, 1.0);
  fragWorldPos = worldPos.xyz;
  fragNormal = mat3(ubo.model) * inNormal;
  gl_Position = ubo.projection * ubo.view * worldPos;
}
