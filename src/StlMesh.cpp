#include "StlMesh.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <glm/geometric.hpp>

namespace {
struct Triangle {
  glm::vec3 normal;
  glm::vec3 v0;
  glm::vec3 v1;
  glm::vec3 v2;
};

void appendTriangle(const Triangle& tri, std::vector<StlVertex>* vertices,
                    std::vector<uint32_t>* indices) {
  const uint32_t base = static_cast<uint32_t>(vertices->size());
  vertices->push_back({tri.v0, tri.normal});
  vertices->push_back({tri.v1, tri.normal});
  vertices->push_back({tri.v2, tri.normal});
  indices->push_back(base + 0);
  indices->push_back(base + 1);
  indices->push_back(base + 2);
}

bool parseFloat3(const std::string& line, const std::string& keyword, glm::vec3* out) {
  std::string trimmed = line;
  trimmed.erase(trimmed.begin(),
                std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char c) {
                  return !std::isspace(c);
                }));
  if (trimmed.rfind(keyword, 0) != 0) {
    return false;
  }

  std::istringstream iss(trimmed.substr(keyword.size()));
  return static_cast<bool>(iss >> out->x >> out->y >> out->z);
}

glm::vec3 faceNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
  const glm::vec3 n = glm::cross(b - a, c - a);
  if (glm::dot(n, n) < 1e-12f) {
    return glm::vec3(0.0f, 1.0f, 0.0f);
  }
  return glm::normalize(n);
}

// Safe little-endian read via memcpy (avoids undefined behavior from
// misaligned type-punned pointer casts).
template <typename T>
T readLE(const uint8_t* bytes) {
  T value{};
  std::memcpy(&value, bytes, sizeof(T));
  return value;
}
}  // namespace

bool StlMesh::loadFromFile(const std::string& path, std::string& error) {
  error.clear();
  vertices_.clear();
  indices_.clear();

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "Could not open STL file: " + path;
    return false;
  }

  file.seekg(0, std::ios::end);
  const std::streamsize fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  if (fileSize <= 0) {
    error = "STL file is empty.";
    return false;
  }

  std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
  file.read(reinterpret_cast<char*>(bytes.data()), fileSize);

  if (!file) {
    error = "Failed to read STL file.";
    return false;
  }

  // Binary STL heuristic: a valid binary file has an 80-byte header followed
  // by a 4-byte triangle count, then exactly (count * 50) bytes of triangle
  // data.  If the total size matches, treat it as binary; otherwise try ASCII.
  bool isBinary = false;
  if (bytes.size() >= 84) {
    const uint32_t triangles = readLE<uint32_t>(bytes.data() + 80);
    const uint64_t expected = 84ull + static_cast<uint64_t>(triangles) * 50ull;
    if (expected == bytes.size()) {
      isBinary = true;
    }
  }

  if (isBinary) {
    return loadBinary(bytes, error);
  }

  const std::string text(bytes.begin(), bytes.end());
  return loadAscii(text, error);
}

bool StlMesh::loadFromMemory(const std::vector<uint8_t>& data, std::string& error) {
  error.clear();
  vertices_.clear();
  indices_.clear();

  if (data.empty()) {
    error = "STL data is empty.";
    return false;
  }

  bool isBinary = false;
  if (data.size() >= 84) {
    const uint32_t triangles = readLE<uint32_t>(data.data() + 80);
    const uint64_t expected = 84ull + static_cast<uint64_t>(triangles) * 50ull;
    if (expected == data.size()) {
      isBinary = true;
    }
  }

  if (isBinary) {
    return loadBinary(data, error);
  }

  const std::string text(data.begin(), data.end());
  return loadAscii(text, error);
}

bool StlMesh::saveAsBinaryToMemory(std::vector<uint8_t>& out, std::string& error) const {
  error.clear();

  if (indices_.size() % 3 != 0 || vertices_.empty()) {
    error = "Mesh has no triangle data to export.";
    return false;
  }

  out.clear();
  out.reserve(84 + (indices_.size() / 3) * 50);

  auto writeBytes = [&](const void* src, size_t n) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
    out.insert(out.end(), p, p + n);
  };

  std::array<char, 80> header{};
  const std::string stamp = "camster STL export";
  std::memcpy(header.data(), stamp.data(), stamp.size());
  writeBytes(header.data(), header.size());

  const uint32_t triangleCount = static_cast<uint32_t>(indices_.size() / 3);
  writeBytes(&triangleCount, sizeof(triangleCount));

  for (size_t i = 0; i < indices_.size(); i += 3) {
    const StlVertex& a = vertices_[indices_[i + 0]];
    const StlVertex& b = vertices_[indices_[i + 1]];
    const StlVertex& c = vertices_[indices_[i + 2]];
    const glm::vec3 n = faceNormal(a.position, b.position, c.position);

    writeBytes(&n.x, sizeof(float));
    writeBytes(&n.y, sizeof(float));
    writeBytes(&n.z, sizeof(float));
    writeBytes(&a.position.x, sizeof(float));
    writeBytes(&a.position.y, sizeof(float));
    writeBytes(&a.position.z, sizeof(float));
    writeBytes(&b.position.x, sizeof(float));
    writeBytes(&b.position.y, sizeof(float));
    writeBytes(&b.position.z, sizeof(float));
    writeBytes(&c.position.x, sizeof(float));
    writeBytes(&c.position.y, sizeof(float));
    writeBytes(&c.position.z, sizeof(float));

    const uint16_t attribute = 0;
    writeBytes(&attribute, sizeof(attribute));
  }
  return true;
}

bool StlMesh::saveAsBinary(const std::string& path, std::string& error) const {
  error.clear();

  if (indices_.size() % 3 != 0 || vertices_.empty()) {
    error = "Mesh has no triangle data to export.";
    return false;
  }

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "Could not open output file: " + path;
    return false;
  }

  std::array<char, 80> header{};
  const std::string stamp = "camster STL export";
  std::memcpy(header.data(), stamp.data(), stamp.size());
  out.write(header.data(), header.size());

  const uint32_t triangleCount = static_cast<uint32_t>(indices_.size() / 3);
  out.write(reinterpret_cast<const char*>(&triangleCount), sizeof(triangleCount));

  for (size_t i = 0; i < indices_.size(); i += 3) {
    const StlVertex& a = vertices_[indices_[i + 0]];
    const StlVertex& b = vertices_[indices_[i + 1]];
    const StlVertex& c = vertices_[indices_[i + 2]];
    const glm::vec3 n = faceNormal(a.position, b.position, c.position);

    out.write(reinterpret_cast<const char*>(&n.x), sizeof(float));
    out.write(reinterpret_cast<const char*>(&n.y), sizeof(float));
    out.write(reinterpret_cast<const char*>(&n.z), sizeof(float));

    out.write(reinterpret_cast<const char*>(&a.position.x), sizeof(float));
    out.write(reinterpret_cast<const char*>(&a.position.y), sizeof(float));
    out.write(reinterpret_cast<const char*>(&a.position.z), sizeof(float));

    out.write(reinterpret_cast<const char*>(&b.position.x), sizeof(float));
    out.write(reinterpret_cast<const char*>(&b.position.y), sizeof(float));
    out.write(reinterpret_cast<const char*>(&b.position.z), sizeof(float));

    out.write(reinterpret_cast<const char*>(&c.position.x), sizeof(float));
    out.write(reinterpret_cast<const char*>(&c.position.y), sizeof(float));
    out.write(reinterpret_cast<const char*>(&c.position.z), sizeof(float));

    const uint16_t attribute = 0;
    out.write(reinterpret_cast<const char*>(&attribute), sizeof(attribute));
  }

  if (!out) {
    error = "Write failed while exporting STL.";
    return false;
  }

  return true;
}

bool StlMesh::saveAsBinaryScaled(const std::string& path, float scale,
                                  std::string& error) const {
  error.clear();

  if (indices_.size() % 3 != 0 || vertices_.empty()) {
    error = "Mesh has no triangle data to export.";
    return false;
  }

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "Could not open output file: " + path;
    return false;
  }

  std::array<char, 80> header{};
  const std::string stamp = "camster STL export";
  std::memcpy(header.data(), stamp.data(), stamp.size());
  out.write(header.data(), header.size());

  const uint32_t triangleCount = static_cast<uint32_t>(indices_.size() / 3);
  out.write(reinterpret_cast<const char*>(&triangleCount), sizeof(triangleCount));

  for (size_t i = 0; i < indices_.size(); i += 3) {
    const glm::vec3 pa = vertices_[indices_[i + 0]].position * scale;
    const glm::vec3 pb = vertices_[indices_[i + 1]].position * scale;
    const glm::vec3 pc = vertices_[indices_[i + 2]].position * scale;
    const glm::vec3 n = faceNormal(pa, pb, pc);

    out.write(reinterpret_cast<const char*>(&n.x), sizeof(float));
    out.write(reinterpret_cast<const char*>(&n.y), sizeof(float));
    out.write(reinterpret_cast<const char*>(&n.z), sizeof(float));

    out.write(reinterpret_cast<const char*>(&pa.x), sizeof(float));
    out.write(reinterpret_cast<const char*>(&pa.y), sizeof(float));
    out.write(reinterpret_cast<const char*>(&pa.z), sizeof(float));

    out.write(reinterpret_cast<const char*>(&pb.x), sizeof(float));
    out.write(reinterpret_cast<const char*>(&pb.y), sizeof(float));
    out.write(reinterpret_cast<const char*>(&pb.z), sizeof(float));

    out.write(reinterpret_cast<const char*>(&pc.x), sizeof(float));
    out.write(reinterpret_cast<const char*>(&pc.y), sizeof(float));
    out.write(reinterpret_cast<const char*>(&pc.z), sizeof(float));

    const uint16_t attribute = 0;
    out.write(reinterpret_cast<const char*>(&attribute), sizeof(attribute));
  }

  if (!out) {
    error = "Write failed while exporting STL.";
    return false;
  }

  return true;
}

bool StlMesh::empty() const { return vertices_.empty() || indices_.empty(); }

const std::vector<StlVertex>& StlMesh::vertices() const { return vertices_; }

const std::vector<uint32_t>& StlMesh::indices() const { return indices_; }

StlMesh StlMesh::makeUnitCube() {
  StlMesh mesh;
  const std::array<glm::vec3, 8> p = {
      glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.5f, -0.5f, -0.5f),
      glm::vec3(0.5f, 0.5f, -0.5f),   glm::vec3(-0.5f, 0.5f, -0.5f),
      glm::vec3(-0.5f, -0.5f, 0.5f),  glm::vec3(0.5f, -0.5f, 0.5f),
      glm::vec3(0.5f, 0.5f, 0.5f),    glm::vec3(-0.5f, 0.5f, 0.5f),
  };

  const std::array<std::array<int, 3>, 12> tris = {
      std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3},
      std::array<int, 3>{4, 6, 5}, std::array<int, 3>{4, 7, 6},
      std::array<int, 3>{0, 4, 5}, std::array<int, 3>{0, 5, 1},
      std::array<int, 3>{3, 2, 6}, std::array<int, 3>{3, 6, 7},
      std::array<int, 3>{1, 5, 6}, std::array<int, 3>{1, 6, 2},
      std::array<int, 3>{0, 3, 7}, std::array<int, 3>{0, 7, 4},
  };

  for (const auto& t : tris) {
    const glm::vec3 n = faceNormal(p[t[0]], p[t[1]], p[t[2]]);
    appendTriangle({n, p[t[0]], p[t[1]], p[t[2]]}, &mesh.vertices_, &mesh.indices_);
  }

  return mesh;
}

StlMesh StlMesh::fromGeometry(std::vector<StlVertex> verts, std::vector<uint32_t> inds) {
  StlMesh mesh;
  mesh.vertices_ = std::move(verts);
  mesh.indices_ = std::move(inds);
  return mesh;
}

void StlMesh::append(const StlMesh& other) {
  append(other, glm::vec3(-1.0f));
}

void StlMesh::append(const StlMesh& other, const glm::vec3& color) {
  const auto base = static_cast<uint32_t>(vertices_.size());
  const bool useOverrideColor = color.x >= 0.0f && color.y >= 0.0f && color.z >= 0.0f;

  vertices_.reserve(vertices_.size() + other.vertices_.size());
  for (const auto& v : other.vertices_) {
    StlVertex out = v;
    if (useOverrideColor) out.color = color;
    vertices_.push_back(out);
  }

  for (uint32_t idx : other.indices_) {
    indices_.push_back(base + idx);
  }
}

bool StlMesh::loadAscii(const std::string& text, std::string& error) {
  std::istringstream iss(text);
  std::string line;

  Triangle current{};
  std::array<glm::vec3, 3> verts{};
  int vertCount = 0;

  while (std::getline(iss, line)) {
    glm::vec3 value(0.0f);
    if (parseFloat3(line, "facet normal", &value)) {
      current.normal = value;
      vertCount = 0;
      continue;
    }

    if (parseFloat3(line, "vertex", &value)) {
      if (vertCount < 3) {
        verts[vertCount++] = value;
      }

      if (vertCount == 3) {
        current.v0 = verts[0];
        current.v1 = verts[1];
        current.v2 = verts[2];
        if (glm::dot(current.normal, current.normal) < 1e-12f) {
          current.normal = faceNormal(current.v0, current.v1, current.v2);
        }
        appendTriangle(current, &vertices_, &indices_);
      }
    }
  }

  if (vertices_.empty()) {
    error = "ASCII STL parse produced no triangles.";
    return false;
  }

  return true;
}

bool StlMesh::loadBinary(const std::vector<uint8_t>& data, std::string& error) {
  if (data.size() < 84) {
    error = "Binary STL data too small.";
    return false;
  }

  const uint32_t triCount = readLE<uint32_t>(data.data() + 80);
  const size_t expected = 84ull + static_cast<size_t>(triCount) * 50ull;
  if (expected > data.size()) {
    error = "Binary STL data truncated.";
    return false;
  }

  size_t offset = 84;
  for (uint32_t i = 0; i < triCount; ++i) {
    Triangle tri{};
    tri.normal.x = readLE<float>(data.data() + offset + 0);
    tri.normal.y = readLE<float>(data.data() + offset + 4);
    tri.normal.z = readLE<float>(data.data() + offset + 8);

    tri.v0.x = readLE<float>(data.data() + offset + 12);
    tri.v0.y = readLE<float>(data.data() + offset + 16);
    tri.v0.z = readLE<float>(data.data() + offset + 20);

    tri.v1.x = readLE<float>(data.data() + offset + 24);
    tri.v1.y = readLE<float>(data.data() + offset + 28);
    tri.v1.z = readLE<float>(data.data() + offset + 32);

    tri.v2.x = readLE<float>(data.data() + offset + 36);
    tri.v2.y = readLE<float>(data.data() + offset + 40);
    tri.v2.z = readLE<float>(data.data() + offset + 44);

    if (glm::dot(tri.normal, tri.normal) < 1e-12f) {
      tri.normal = faceNormal(tri.v0, tri.v1, tri.v2);
    }

    appendTriangle(tri, &vertices_, &indices_);
    offset += 50;
  }

  if (vertices_.empty()) {
    error = "Binary STL parse produced no triangles.";
    return false;
  }

  return true;
}
