#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// Per-vertex data fed to the GPU.  STL stores one normal per face, so all
// three vertices in a triangle share the same normal (flat shading).
struct StlVertex {
  glm::vec3 position;
  glm::vec3 normal;
};

// Loads STL mesh files (both ASCII and binary variants) and can export back
// to binary STL.  Also provides a built-in unit cube for startup display.
class StlMesh {
 public:
  bool loadFromFile(const std::string& path, std::string& error);
  bool saveAsBinary(const std::string& path, std::string& error) const;
  // Export with uniform scale applied to all vertex positions (e.g. mm→inches).
  bool saveAsBinaryScaled(const std::string& path, float scale, std::string& error) const;

  bool empty() const;
  const std::vector<StlVertex>& vertices() const;
  const std::vector<uint32_t>& indices() const;

  static StlMesh makeUnitCube();
  static StlMesh fromGeometry(std::vector<StlVertex> verts, std::vector<uint32_t> inds);
  void append(const StlMesh& other);

 private:
  bool loadAscii(const std::string& text, std::string& error);
  bool loadBinary(const std::vector<uint8_t>& data, std::string& error);

  std::vector<StlVertex> vertices_;
  std::vector<uint32_t> indices_;
};
