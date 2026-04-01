#include "interop/Step.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

namespace {

std::vector<float> parseNumberList(const std::string& text) {
  std::vector<float> values;
  std::string token;
  token.reserve(32);
  auto flush = [&]() {
    if (token.empty()) return;
    values.push_back(std::strtof(token.c_str(), nullptr));
    token.clear();
  };

  for (char c : text) {
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' ||
        c == 'e' || c == 'E') {
      token.push_back(c);
    } else {
      flush();
    }
  }
  flush();
  return values;
}

std::vector<int> parseIntList(const std::string& text) {
  std::vector<int> values;
  std::string token;
  token.reserve(16);
  auto flush = [&]() {
    if (token.empty()) return;
    values.push_back(std::strtol(token.c_str(), nullptr, 10));
    token.clear();
  };

  for (char c : text) {
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+') {
      token.push_back(c);
    } else {
      flush();
    }
  }
  flush();
  return values;
}

}  // namespace

bool exportStepMesh(const std::string& path, const StlMesh& mesh, std::string& error) {
  if (mesh.empty()) {
    error = "Mesh has no data to export";
    return false;
  }

  std::ofstream out(path);
  if (!out) {
    error = "Failed to open STEP output path";
    return false;
  }

  out << "ISO-10303-21;\n";
  out << "HEADER;\n";
  out << "FILE_DESCRIPTION(('camster STEP baseline'),'2;1');\n";
  out << "FILE_NAME('camster.step','2026-04-01T00:00:00',('camster'),('camster'),'camster','camster','');\n";
  out << "FILE_SCHEMA(('AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF'));\n";
  out << "ENDSEC;\n";
  out << "DATA;\n";

  out << "#10=CARTESIAN_POINT_LIST_3D('',(";
  const auto& verts = mesh.vertices();
  for (size_t i = 0; i < verts.size(); ++i) {
    const auto& p = verts[i].position;
    out << "(" << p.x << "," << p.y << "," << p.z << ")";
    if (i + 1 < verts.size()) out << ",";
  }
  out << "));\n";

  out << "#11=TRIANGULATED_FACE_SET('',#10,$,.T.,(";
  const auto& inds = mesh.indices();
  for (size_t i = 0; i + 2 < inds.size(); i += 3) {
    out << "(" << (inds[i] + 1) << "," << (inds[i + 1] + 1) << "," << (inds[i + 2] + 1) << ")";
    if (i + 3 < inds.size()) out << ",";
  }
  out << "));\n";

  out << "ENDSEC;\n";
  out << "END-ISO-10303-21;\n";
  return true;
}

bool importStepMesh(const std::string& path, StlMesh& mesh, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = "Failed to open STEP file";
    return false;
  }

  std::stringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();

  const size_t plistPos = text.find("CARTESIAN_POINT_LIST_3D");
  const size_t tfsPos = text.find("TRIANGULATED_FACE_SET");
  if (plistPos == std::string::npos || tfsPos == std::string::npos) {
    error = "Unsupported STEP content (needs TRIANGULATED_FACE_SET baseline)";
    return false;
  }

  const size_t pStart = text.find("((", plistPos);
  const size_t pEnd = text.find("));", pStart);
  const size_t fStart = text.find("((", tfsPos);
  const size_t fEnd = text.find("));", fStart);
  if (pStart == std::string::npos || pEnd == std::string::npos ||
      fStart == std::string::npos || fEnd == std::string::npos) {
    error = "Malformed STEP baseline geometry";
    return false;
  }

  const std::vector<float> nums = parseNumberList(text.substr(pStart, pEnd - pStart));
  if (nums.size() < 9 || nums.size() % 3 != 0) {
    error = "Invalid point list in STEP file";
    return false;
  }

  std::vector<StlVertex> verts(nums.size() / 3);
  for (size_t i = 0; i < verts.size(); ++i) {
    verts[i].position = {nums[i * 3 + 0], nums[i * 3 + 1], nums[i * 3 + 2]};
    verts[i].normal = {0.0f, 0.0f, 1.0f};
  }

  const std::vector<int> faces = parseIntList(text.substr(fStart, fEnd - fStart));
  if (faces.size() < 3 || faces.size() % 3 != 0) {
    error = "Invalid triangle list in STEP file";
    return false;
  }

  std::vector<uint32_t> inds;
  inds.reserve(faces.size());
  for (int idx : faces) {
    if (idx <= 0 || static_cast<size_t>(idx) > verts.size()) {
      error = "STEP face index out of range";
      return false;
    }
    inds.push_back(static_cast<uint32_t>(idx - 1));
  }

  for (size_t i = 0; i + 2 < inds.size(); i += 3) {
    const glm::vec3 a = verts[inds[i]].position;
    const glm::vec3 b = verts[inds[i + 1]].position;
    const glm::vec3 c = verts[inds[i + 2]].position;
    glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) {
      n = {0.0f, 0.0f, 1.0f};
    }
    verts[inds[i]].normal = n;
    verts[inds[i + 1]].normal = n;
    verts[inds[i + 2]].normal = n;
  }

  mesh = StlMesh::fromGeometry(std::move(verts), std::move(inds));
  return true;
}
