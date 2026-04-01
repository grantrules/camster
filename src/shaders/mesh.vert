// Vertex shader: transforms mesh positions from model space to clip space and
// passes the world-space normal/position to the fragment shader for lighting.
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(binding = 0) uniform UBO {
  mat4 model;
  mat4 view;
  mat4 projection;
  vec4 options;   // options.x: normals visualization toggle (0 or 1)
} ubo;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec3 fragColor;

void main() {
  vec4 worldPos = ubo.model * vec4(inPos, 1.0);
  fragWorldPos = worldPos.xyz;
  // Rotate normal by the model matrix (ignoring translation via mat3 extract).
  fragNormal = mat3(ubo.model) * inNormal;
  fragColor = inColor;
  gl_Position = ubo.projection * ubo.view * worldPos;
}
