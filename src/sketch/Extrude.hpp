#pragma once

#include <vector>

#include <glm/vec2.hpp>

#include "Scene.hpp"
#include "StlMesh.hpp"

// Generate a triangulated StlMesh by extruding closed 2D profiles along
// the sketch plane's normal by the given distance (in mm).
StlMesh extrudeMesh(const std::vector<std::vector<glm::vec2>>& profiles,
                    SketchPlane plane, float distanceMm);
