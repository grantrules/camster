// Vertex shader for colored lines (gizmo, grid, sketch geometry).
// Reuses the same UBO layout as the mesh shader so both pipelines can share
// one descriptor set.
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(binding = 0) uniform UBO {
  mat4 model;
  mat4 view;
  mat4 projection;
  vec4 options;
} ubo;

layout(location = 0) out vec4 fragColor;

void main() {
  fragColor = inColor;
  gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPos, 1.0);
}
