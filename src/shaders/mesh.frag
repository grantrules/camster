// Fragment shader: either visualizes surface normals as RGB colors (debug mode)
// or applies simple directional diffuse + ambient lighting.
#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec3 fragColor;

layout(binding = 0) uniform UBO {
  mat4 model;
  mat4 view;
  mat4 projection;
  vec4 options;   // options.x: normals visualization toggle (0 or 1)
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 n = normalize(fragNormal);

  // Normals debug view: remap [-1,1] normal components to [0,1] RGB.
  if (ubo.options.x > 0.5) {
    outColor = vec4(n * 0.5 + 0.5, 1.0);
    return;
  }

  // Simple Lambertian diffuse with a fixed directional light.
  vec3 lightDir = normalize(vec3(-0.4, 0.8, 0.6));
  float diff = max(dot(n, lightDir), 0.0);
  vec3 ambient = vec3(0.18, 0.2, 0.22);
  vec3 base = fragColor;
  vec3 color = ambient + diff * base;
  outColor = vec4(color, 1.0);
}
