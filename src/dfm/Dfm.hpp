#pragma once

#include <string>
#include <vector>

#include "StlMesh.hpp"

struct DfmIssue {
  std::string code;
  std::string message;
  bool severe = false;
};

struct DfmReport {
  float estimatedMinWallMm = 0.0f;
  float estimatedMinRadiusMm = 0.0f;
  int drillableFeatureCount = 0;
  std::vector<DfmIssue> issues;
};

bool runDfmChecks(const StlMesh& mesh, DfmReport& report, std::string& error);
