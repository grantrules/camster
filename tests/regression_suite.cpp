#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

#include "cam/Cam.hpp"
#include "dfm/Dfm.hpp"
#include "drawing/Drawing.hpp"
#include "interop/Step.hpp"
#include "sketch/Extrude.hpp"

namespace {

uint64_t hashMesh(const StlMesh& mesh) {
  const auto q = [](float v) -> uint32_t {
    return static_cast<uint32_t>(std::llround(v * 10000.0f));
  };
  uint64_t h = 1469598103934665603ull;
  const auto mix = [&](uint32_t v) {
    h ^= static_cast<uint64_t>(v);
    h *= 1099511628211ull;
  };
  for (const auto& v : mesh.vertices()) {
    mix(q(v.position.x));
    mix(q(v.position.y));
    mix(q(v.position.z));
    mix(q(v.normal.x));
    mix(q(v.normal.y));
    mix(q(v.normal.z));
  }
  for (uint32_t idx : mesh.indices()) mix(idx);
  return h;
}

std::vector<std::vector<glm::vec2>> unitSquareProfile() {
  return {{{-10.0f, -10.0f}, {10.0f, -10.0f}, {10.0f, 10.0f}, {-10.0f, 10.0f}, {-10.0f, -10.0f}}};
}

bool geometryDeterminismTest() {
  const auto profile = unitSquareProfile();
  const StlMesh a = extrudeMesh(profile, SketchPlane::XY, 20.0f);
  const StlMesh b = extrudeMesh(profile, SketchPlane::XY, 20.0f);
  if (a.empty() || b.empty()) {
    std::cerr << "geometryDeterminismTest: extrude returned empty mesh\n";
    return false;
  }
  if (hashMesh(a) != hashMesh(b)) {
    std::cerr << "geometryDeterminismTest: hash mismatch\n";
    return false;
  }
  return true;
}

bool camSnapshotTest() {
  std::vector<CamToolPreset> tools;
  CamStockSettings stock;
  CamOperationBuilderState builder;
  initializeCamDefaults(tools, stock, builder);

  builder.type = CamOperationType::Pocket;
  builder.sourceObject = 0;
  builder.sourceSketch = -1;
  builder.toolIndex = 0;
  builder.topZMm = stock.originMm.z + stock.sizeMm.z;
  builder.depthMm = 3.0f;
  builder.stepDownMm = 1.0f;
  builder.stepoverPercent = 50.0f;

  StlMesh src = extrudeMesh(unitSquareProfile(), SketchPlane::XY, 15.0f);
  std::vector<StlMesh> scene{src};
  std::vector<SketchEntry> sketches;
  std::vector<ReferencePointEntry> points;

  CamOperation op;
  std::string err;
  if (!generateCamOperation(builder, tools, stock, scene, sketches, points, op, err)) {
    std::cerr << "camSnapshotTest: generation failed: " << err << "\n";
    return false;
  }
  if (op.segments.empty() || op.estimatedLengthMm <= 0.0f || op.estimatedMinutes <= 0.0f) {
    std::cerr << "camSnapshotTest: invalid operation metrics\n";
    return false;
  }

  const std::filesystem::path snapDir = "dist-debug-docker/regression";
  std::error_code ec;
  std::filesystem::create_directories(snapDir, ec);
  const std::filesystem::path snapPath = snapDir / "cam_snapshot.txt";

  std::ofstream out(snapPath.string());
  if (!out) {
    std::cerr << "camSnapshotTest: cannot write snapshot\n";
    return false;
  }
  out << "segments=" << op.segments.size() << "\n";
  out << "length_mm=" << op.estimatedLengthMm << "\n";
  out << "minutes=" << op.estimatedMinutes << "\n";
  return true;
}

bool drawingAndInteropTest() {
  const StlMesh mesh = revolveMesh(unitSquareProfile(), SketchPlane::XY, 0.0f, 0, 270.0f);
  if (mesh.empty()) {
    std::cerr << "drawingAndInteropTest: revolve returned empty mesh\n";
    return false;
  }

  DrawingSheet sheet;
  std::string err;
  if (!buildDrawingSheet(mesh, "RegressionPart", 0.5f, sheet, err)) {
    std::cerr << "drawingAndInteropTest: buildDrawingSheet failed: " << err << "\n";
    return false;
  }

  const std::filesystem::path outDir = "dist-debug-docker/regression";
  std::error_code ec;
  std::filesystem::create_directories(outDir, ec);

  if (!exportDrawingDxf((outDir / "regression.dxf").string(), sheet, err)) {
    std::cerr << "drawingAndInteropTest: DXF export failed: " << err << "\n";
    return false;
  }
  if (!exportDrawingPdf((outDir / "regression.pdf").string(), sheet, err)) {
    std::cerr << "drawingAndInteropTest: PDF export failed: " << err << "\n";
    return false;
  }

  const std::filesystem::path stepPath = outDir / "regression.step";
  if (!exportStepMesh(stepPath.string(), mesh, err)) {
    std::cerr << "drawingAndInteropTest: STEP export failed: " << err << "\n";
    return false;
  }
  StlMesh imported;
  if (!importStepMesh(stepPath.string(), imported, err)) {
    std::cerr << "drawingAndInteropTest: STEP import failed: " << err << "\n";
    return false;
  }
  if (imported.empty()) {
    std::cerr << "drawingAndInteropTest: imported mesh empty\n";
    return false;
  }
  return true;
}

bool dfmTest() {
  const StlMesh mesh = extrudeMesh(unitSquareProfile(), SketchPlane::XY, 4.0f);
  DfmReport report;
  std::string err;
  if (!runDfmChecks(mesh, report, err)) {
    std::cerr << "dfmTest: runDfmChecks failed: " << err << "\n";
    return false;
  }
  if (report.estimatedMinWallMm <= 0.0f || report.estimatedMinRadiusMm <= 0.0f) {
    std::cerr << "dfmTest: invalid report values\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  bool ok = true;
  ok = ok && geometryDeterminismTest();
  ok = ok && camSnapshotTest();
  ok = ok && drawingAndInteropTest();
  ok = ok && dfmTest();

  if (!ok) {
    std::cerr << "Regression suite FAILED\n";
    return 1;
  }

  std::cout << "Regression suite PASSED\n";
  return 0;
}
