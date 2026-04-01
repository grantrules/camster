#include "dfm/Dfm.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {

struct Quant2 {
  int x = 0;
  int y = 0;
  bool operator==(const Quant2& rhs) const { return x == rhs.x && y == rhs.y; }
};

struct Quant2Hash {
  size_t operator()(const Quant2& q) const {
    return static_cast<size_t>((q.x * 73856093) ^ (q.y * 19349663));
  }
};

}  // namespace

bool runDfmChecks(const StlMesh& mesh, DfmReport& report, std::string& error) {
  report = {};
  const auto& verts = mesh.vertices();
  const auto& inds = mesh.indices();
  if (verts.empty() || inds.empty()) {
    error = "No mesh data for DFM checks";
    return false;
  }

  float minEdge = std::numeric_limits<float>::max();
  glm::vec3 bbMin = verts.front().position;
  glm::vec3 bbMax = verts.front().position;

  std::unordered_map<Quant2, int, Quant2Hash> verticalClusters;
  const auto quant = [](float v) { return static_cast<int>(std::llround(v * 5.0f)); };

  for (const auto& v : verts) {
    bbMin = glm::min(bbMin, v.position);
    bbMax = glm::max(bbMax, v.position);
    Quant2 key{quant(v.position.x), quant(v.position.y)};
    verticalClusters[key] += 1;
  }

  for (size_t i = 0; i + 2 < inds.size(); i += 3) {
    const glm::vec3 a = verts[inds[i]].position;
    const glm::vec3 b = verts[inds[i + 1]].position;
    const glm::vec3 c = verts[inds[i + 2]].position;
    minEdge = std::min(minEdge, glm::length(a - b));
    minEdge = std::min(minEdge, glm::length(b - c));
    minEdge = std::min(minEdge, glm::length(c - a));
  }

  const glm::vec3 extents = bbMax - bbMin;
  report.estimatedMinWallMm = std::max(0.0f, minEdge);
  report.estimatedMinRadiusMm = std::max(0.0f, minEdge * 0.5f);

  int candidateDrill = 0;
  for (const auto& [_, count] : verticalClusters) {
    if (count >= 6) ++candidateDrill;
  }
  report.drillableFeatureCount = candidateDrill;

  if (report.estimatedMinWallMm < 1.0f) {
    report.issues.push_back({"WALL_THIN", "Estimated minimum wall is below 1.0 mm", true});
  }
  if (report.estimatedMinRadiusMm < 0.5f) {
    report.issues.push_back({"RADIUS_TIGHT", "Estimated minimum corner radius is below 0.5 mm", false});
  }
  if (candidateDrill == 0) {
    report.issues.push_back({"DRILLABILITY_LOW", "No clear vertical drillable feature clusters detected", false});
  }
  if (extents.z < 2.0f) {
    report.issues.push_back({"FLAT_PART", "Part thickness is very low relative to machining operations", false});
  }

  return true;
}
