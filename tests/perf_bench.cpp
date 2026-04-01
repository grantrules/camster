#include <chrono>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include "cam/Cam.hpp"
#include "sketch/Extrude.hpp"

namespace {

std::vector<std::vector<glm::vec2>> makeCircleLikeProfile(int n, float radius) {
  std::vector<glm::vec2> ring;
  ring.reserve(static_cast<size_t>(n) + 1);
  for (int i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(n);
    const float a = t * 6.28318530718f;
    ring.push_back({std::cos(a) * radius, std::sin(a) * radius});
  }
  ring.push_back(ring.front());
  return {ring};
}

}  // namespace

int main() {
  using clock = std::chrono::steady_clock;
  const auto profile = makeCircleLikeProfile(96, 25.0f);

  const auto t0 = clock::now();
  StlMesh mesh;
  for (int i = 0; i < 20; ++i) {
    mesh = sweepMesh(profile, SketchPlane::XY, 0.0f, 2, 50.0f + i);
  }
  const auto t1 = clock::now();

  std::vector<CamToolPreset> tools;
  CamStockSettings stock;
  CamOperationBuilderState builder;
  initializeCamDefaults(tools, stock, builder);
  builder.type = CamOperationType::Contour;
  builder.sourceObject = 0;
  builder.depthMm = 4.0f;
  builder.stepDownMm = 0.8f;

  std::vector<StlMesh> scene{mesh};
  std::vector<SketchEntry> sketches;
  std::vector<ReferencePointEntry> points;
  CamOperation op;
  std::string err;
  const auto t2 = clock::now();
  for (int i = 0; i < 12; ++i) {
    if (!generateCamOperation(builder, tools, stock, scene, sketches, points, op, err)) {
      std::cerr << "CAM perf benchmark failed: " << err << "\n";
      return 1;
    }
  }
  const auto t3 = clock::now();

  const double geomMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const double camMs = std::chrono::duration<double, std::milli>(t3 - t2).count();

  constexpr double kGeomBudgetMs = 400.0;
  constexpr double kCamBudgetMs = 250.0;

  std::ofstream out("dist-debug-docker/perf-bench.txt");
  out << "geometry_ms=" << geomMs << "\n";
  out << "cam_ms=" << camMs << "\n";
  out << "geom_budget_ms=" << kGeomBudgetMs << "\n";
  out << "cam_budget_ms=" << kCamBudgetMs << "\n";

  bool ok = true;
  if (geomMs > kGeomBudgetMs) {
    std::cerr << "Geometry benchmark exceeded budget: " << geomMs << " ms\n";
    ok = false;
  }
  if (camMs > kCamBudgetMs) {
    std::cerr << "CAM benchmark exceeded budget: " << camMs << " ms\n";
    ok = false;
  }

  if (!ok) return 2;
  std::cout << "Perf benchmark PASSED\n";
  return 0;
}
