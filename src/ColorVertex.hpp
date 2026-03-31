#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// Vertex format for colored line rendering (gizmo axes, grid, sketch geometry).
// Unlike StlVertex which carries a surface normal, this carries an RGBA color.
struct ColorVertex {
  glm::vec3 position;
  glm::vec4 color;
};
