#pragma once

#include <array>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "ColorVertex.hpp"
#include "ProjectTypes.hpp"
#include "StlMesh.hpp"

enum class CamOperationType { Facing, Pocket, Contour, Drilling };
enum class CamToolType { FlatEndMill, BallEndMill, Drill };
enum class CamPostProcessor { Grbl };

struct CamToolPreset {
  std::array<char, 64> name{};
  CamToolType type = CamToolType::FlatEndMill;
  float diameterMm = 6.0f;
  float feedRateMmPerMin = 800.0f;
  float plungeRateMmPerMin = 250.0f;
  float spindleRpm = 12000.0f;
  float maxStepDownMm = 1.5f;
  float stepover = 0.5f;
};

struct CamStockSettings {
  glm::vec3 originMm{-60.0f, -60.0f, 0.0f};
  glm::vec3 sizeMm{120.0f, 120.0f, 40.0f};
  glm::vec3 wcsOffsetMm{0.0f, 0.0f, 40.0f};
  float safeRetractMm = 8.0f;
};

struct CamPathSegment {
  glm::vec3 start{0.0f};
  glm::vec3 end{0.0f};
  bool rapid = false;
  bool plunge = false;
};

struct CamOperationWarning {
  std::string message;
  bool severe = false;
};

struct CamOperation {
  CamOperationType type = CamOperationType::Facing;
  std::string name;
  int toolIndex = -1;
  int sourceObject = -1;
  int sourceSketch = -1;
  float topZMm = 0.0f;
  float bottomZMm = 0.0f;
  float stepoverMm = 0.0f;
  float stepDownMm = 0.0f;
  float clearanceMm = 0.0f;
  std::vector<CamPathSegment> segments;
  std::vector<CamOperationWarning> warnings;
  float estimatedLengthMm = 0.0f;
  float estimatedMinutes = 0.0f;
};

struct CamOperationBuilderState {
  CamOperationType type = CamOperationType::Facing;
  int sourceObject = -1;
  int sourceSketch = -1;
  int toolIndex = 0;
  float topZMm = 40.0f;
  float depthMm = 1.0f;
  float stepDownMm = 1.0f;
  float stepoverPercent = 55.0f;
  float clearanceMm = 5.0f;
};

const char* camOperationTypeLabel(CamOperationType type);
const char* camToolTypeLabel(CamToolType type);
const char* camPostProcessorLabel(CamPostProcessor post);

void initializeCamDefaults(std::vector<CamToolPreset>& tools, CamStockSettings& stock,
                           CamOperationBuilderState& builder);
void resetCamSession(CamOperationBuilderState& builder, std::vector<CamOperation>& operations,
                     int selectedObject, int selectedSketch, const CamStockSettings& stock);

bool generateCamOperation(const CamOperationBuilderState& builder,
                          const std::vector<CamToolPreset>& tools,
                          const CamStockSettings& stock,
                          const std::vector<StlMesh>& sceneObjects,
                          const std::vector<SketchEntry>& sketches,
                          const std::vector<ReferencePointEntry>& referencePoints,
                          CamOperation& operation,
                          std::string& error);

void appendCamPreviewLines(const CamStockSettings& stock,
                           const std::vector<CamOperation>& operations,
                           std::vector<ColorVertex>& lines);

bool exportCamGcode(const std::string& path,
                    CamPostProcessor post,
                    const CamStockSettings& stock,
                    const std::vector<CamToolPreset>& tools,
                    const std::vector<CamOperation>& operations,
                    std::string& error);