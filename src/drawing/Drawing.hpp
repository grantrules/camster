#pragma once

#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include "StlMesh.hpp"

enum class DrawingViewKind { Front, Top, Right, Section };

struct DrawingView {
  DrawingViewKind kind = DrawingViewKind::Front;
  std::string label;
  float widthMm = 0.0f;
  float heightMm = 0.0f;
  std::vector<glm::vec2> outline;
};

struct DrawingDimension {
  std::string label;
  float valueMm = 0.0f;
};

struct DrawingSheet {
  std::string title;
  int sourceObject = -1;
  float sectionRatio = 0.5f;
  std::vector<DrawingView> views;
  std::vector<DrawingDimension> dimensions;
};

bool buildDrawingSheet(const StlMesh& mesh, const std::string& objectName,
                       float sectionRatio, DrawingSheet& out, std::string& error);

bool exportDrawingDxf(const std::string& path, const DrawingSheet& sheet,
                      std::string& error);
bool exportDrawingPdf(const std::string& path, const DrawingSheet& sheet,
                      std::string& error);
