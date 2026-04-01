#pragma once

#include <vector>

#include <glm/vec2.hpp>

#include "Scene.hpp"
#include "StlMesh.hpp"

// Generate a triangulated StlMesh by extruding closed 2D profiles along
// the sketch plane's normal by the given distance (in mm).
StlMesh extrudeMesh(const std::vector<std::vector<glm::vec2>>& profiles,
                    SketchPlane plane, float distanceMm,
                    const std::vector<std::vector<glm::vec2>>& holeProfiles = {});
StlMesh revolveMesh(const std::vector<std::vector<glm::vec2>>& profiles,
                    SketchPlane plane, float planeOffsetMm,
                    int axisMode, float angleDegrees, int segments = 48);
StlMesh sweepMesh(const std::vector<std::vector<glm::vec2>>& profiles,
                  SketchPlane plane, float planeOffsetMm,
                  int axisMode, float distanceMm);
StlMesh loftMesh(const std::vector<std::vector<glm::vec2>>& profilesA,
                 SketchPlane planeA, float offsetA,
                 const std::vector<std::vector<glm::vec2>>& profilesB,
                 SketchPlane planeB, float offsetB,
                 int samples = 64);
StlMesh shellMesh(const StlMesh& mesh, float thicknessMm);
