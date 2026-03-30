#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct StlVertex {
  glm::vec3 position;
  glm::vec3 normal;
};

class StlMesh {
 public:
  bool loadFromFile(const std::string& path, std::string& error);
  bool saveAsBinary(const std::string& path, std::string& error) const;

  bool empty() const;
  const std::vector<StlVertex>& vertices() const;
  const std::vector<uint32_t>& indices() const;

  static StlMesh makeUnitCube();

 private:
  bool loadAscii(const std::string& text, std::string& error);
  bool loadBinary(const std::vector<uint8_t>& data, std::string& error);

  std::vector<StlVertex> vertices_;
  std::vector<uint32_t> indices_;
};
