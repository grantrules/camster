#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;

layout(binding = 0) uniform UBO {
  mat4 model;
  mat4 view;
  mat4 projection;
  vec4 options;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 n = normalize(fragNormal);
  if (ubo.options.x > 0.5) {
    outColor = vec4(n * 0.5 + 0.5, 1.0);
    return;
  }

  vec3 lightDir = normalize(vec3(-0.4, 0.8, 0.6));
  float diff = max(dot(n, lightDir), 0.0);
  vec3 ambient = vec3(0.18, 0.2, 0.22);
  vec3 base = vec3(0.70, 0.74, 0.80);
  vec3 color = ambient + diff * base;
  outColor = vec4(color, 1.0);
}
