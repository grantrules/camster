#include "sketch/Extrude.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/geometric.hpp>

namespace {

float cross2D(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

int planeWindingSign(SketchPlane plane) {
  // toWorld basis orientation relative to planeNormal:
  // XY: +1, XZ: -1, YZ: +1
  switch (plane) {
    case SketchPlane::XY: return 1;
    case SketchPlane::XZ: return -1;
    case SketchPlane::YZ: return 1;
  }
  return 1;
}

bool pointInTriangle(glm::vec2 pt, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
  const float d1 = cross2D(b - a, pt - a);
  const float d2 = cross2D(c - b, pt - b);
  const float d3 = cross2D(a - c, pt - c);
  const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
  const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
  return !(hasNeg && hasPos);
}

bool pointInPolygon(glm::vec2 pt, const std::vector<glm::vec2>& poly) {
  size_t n = poly.size();
  if (n > 0 && glm::length(poly.front() - poly.back()) < 0.001f) --n;
  if (n < 3) return false;

  bool inside = false;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const glm::vec2& a = poly[i];
    const glm::vec2& b = poly[j];
    const bool intersect = ((a.y > pt.y) != (b.y > pt.y)) &&
                           (pt.x < (b.x - a.x) * (pt.y - a.y) / ((b.y - a.y) + 1e-12f) + a.x);
    if (intersect) inside = !inside;
  }
  return inside;
}

glm::vec2 polygonCentroid(const std::vector<glm::vec2>& poly) {
  size_t n = poly.size();
  if (n > 0 && glm::length(poly.front() - poly.back()) < 0.001f) --n;
  if (n == 0) return {0.0f, 0.0f};
  glm::vec2 c{0.0f, 0.0f};
  for (size_t i = 0; i < n; ++i) c += poly[i];
  return c / static_cast<float>(n);
}

// Ear-clipping triangulation for a simple polygon.
// Returns index triples into the input point array.
std::vector<std::array<size_t, 3>> earClip(const std::vector<glm::vec2>& poly) {
  std::vector<std::array<size_t, 3>> tris;

  size_t n = poly.size();
  if (n > 0 && glm::length(poly.front() - poly.back()) < 0.001f) --n;
  if (n < 3) return tris;

  // Compute signed area to ensure CCW winding.
  float area = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    area += cross2D(poly[i], poly[j]);
  }

  std::vector<size_t> idx(n);
  for (size_t i = 0; i < n; ++i) idx[i] = i;
  if (area < 0.0f) std::reverse(idx.begin(), idx.end());

  while (idx.size() > 2) {
    bool earFound = false;
    const size_t sz = idx.size();

    for (size_t i = 0; i < sz; ++i) {
      const size_t prev = idx[(i + sz - 1) % sz];
      const size_t curr = idx[i];
      const size_t next = idx[(i + 1) % sz];

      // Convex test (CCW cross product > 0).
      if (cross2D(poly[curr] - poly[prev], poly[next] - poly[curr]) <= 0.0f) continue;

      // Ensure no other vertex falls inside this ear triangle.
      bool inside = false;
      for (size_t k = 0; k < sz; ++k) {
        const size_t vi = idx[k];
        if (vi == prev || vi == curr || vi == next) continue;
        if (pointInTriangle(poly[vi], poly[prev], poly[curr], poly[next])) {
          inside = true;
          break;
        }
      }

      if (!inside) {
        tris.push_back({prev, curr, next});
        idx.erase(idx.begin() + static_cast<ptrdiff_t>(i));
        earFound = true;
        break;
      }
    }

    if (!earFound) break;  // degenerate polygon
  }

  return tris;
}

}  // namespace

StlMesh extrudeMesh(const std::vector<std::vector<glm::vec2>>& profiles,
                    SketchPlane plane, float distanceMm,
                    const std::vector<std::vector<glm::vec2>>& holeProfiles) {
  const glm::vec3 normal = planeNormal(plane);
  const glm::vec3 offset = normal * distanceMm;
  const int windingSign = planeWindingSign(plane);

  std::vector<StlVertex> vertices;
  std::vector<uint32_t> indices;

  for (const auto& profile : profiles) {
    size_t n = profile.size();
    if (n > 0 && glm::length(profile.front() - profile.back()) < 0.001f) --n;
    if (n < 3) continue;

    // Build 3D front and back vertices.
    std::vector<glm::vec3> front(n), back(n);
    for (size_t i = 0; i < n; ++i) {
      front[i] = toWorld(profile[i], plane);
      back[i] = front[i] + offset;
    }

    const auto tris = earClip(profile);

    // Front cap (faces away from extrude direction). Skip triangles whose
    // centroids fall inside any hole polygon contained by this profile.
    const glm::vec3 frontN = (distanceMm >= 0.0f) ? -normal : normal;
    const int frontDirSign = (distanceMm >= 0.0f) ? -1 : 1;
    const bool frontUseCcw = (windingSign == frontDirSign);
    for (const auto& tri : tris) {
      const glm::vec2 triCenter = (profile[tri[0]] + profile[tri[1]] + profile[tri[2]]) / 3.0f;
      bool inHole = false;
      for (const auto& hole : holeProfiles) {
        const glm::vec2 hCenter = polygonCentroid(hole);
        if (!pointInPolygon(hCenter, profile)) continue;
        if (pointInPolygon(triCenter, hole)) {
          inHole = true;
          break;
        }
      }
      if (inHole) continue;

      const auto base = static_cast<uint32_t>(vertices.size());
      if (frontUseCcw) {
        vertices.push_back({front[tri[0]], frontN});
        vertices.push_back({front[tri[1]], frontN});
        vertices.push_back({front[tri[2]], frontN});
      } else {
        vertices.push_back({front[tri[2]], frontN});
        vertices.push_back({front[tri[1]], frontN});
        vertices.push_back({front[tri[0]], frontN});
      }
      indices.push_back(base);
      indices.push_back(base + 1);
      indices.push_back(base + 2);
    }

    // Back cap with opposite outward winding from front cap.
    const glm::vec3 backN = -frontN;
    const bool backUseCcw = !frontUseCcw;
    for (const auto& tri : tris) {
      const glm::vec2 triCenter = (profile[tri[0]] + profile[tri[1]] + profile[tri[2]]) / 3.0f;
      bool inHole = false;
      for (const auto& hole : holeProfiles) {
        const glm::vec2 hCenter = polygonCentroid(hole);
        if (!pointInPolygon(hCenter, profile)) continue;
        if (pointInPolygon(triCenter, hole)) {
          inHole = true;
          break;
        }
      }
      if (inHole) continue;

      const auto base = static_cast<uint32_t>(vertices.size());
      if (backUseCcw) {
        vertices.push_back({back[tri[0]], backN});
        vertices.push_back({back[tri[1]], backN});
        vertices.push_back({back[tri[2]], backN});
      } else {
        vertices.push_back({back[tri[2]], backN});
        vertices.push_back({back[tri[1]], backN});
        vertices.push_back({back[tri[0]], backN});
      }
      indices.push_back(base);
      indices.push_back(base + 1);
      indices.push_back(base + 2);
    }

    // Side walls — one quad (two triangles) per edge.
    for (size_t i = 0; i < n; ++i) {
      const size_t j = (i + 1) % n;

      const glm::vec3 edge = front[j] - front[i];
      glm::vec3 sideN = glm::cross(edge, offset);
      const float sideLen = glm::length(sideN);
      if (sideLen > 1e-8f) {
        sideN /= sideLen;
      } else {
        sideN = normal;
      }

      const auto base = static_cast<uint32_t>(vertices.size());
      vertices.push_back({front[i], sideN});
      vertices.push_back({front[j], sideN});
      vertices.push_back({back[j], sideN});
      vertices.push_back({back[i], sideN});

      indices.push_back(base);
      indices.push_back(base + 1);
      indices.push_back(base + 2);
      indices.push_back(base);
      indices.push_back(base + 2);
      indices.push_back(base + 3);
    }
  }

  // Hole side walls with inward-facing normals.
  for (const auto& hole : holeProfiles) {
    size_t n = hole.size();
    if (n > 0 && glm::length(hole.front() - hole.back()) < 0.001f) --n;
    if (n < 3) continue;

    std::vector<glm::vec3> front(n), back(n);
    for (size_t i = 0; i < n; ++i) {
      front[i] = toWorld(hole[i], plane);
      back[i] = front[i] + offset;
    }

    for (size_t i = 0; i < n; ++i) {
      const size_t j = (i + 1) % n;
      const glm::vec3 edge = front[j] - front[i];
      glm::vec3 sideN = glm::cross(offset, edge);  // inward for hole wall
      const float sideLen = glm::length(sideN);
      if (sideLen > 1e-8f) {
        sideN /= sideLen;
      } else {
        sideN = -normal;
      }

      const auto base = static_cast<uint32_t>(vertices.size());
      vertices.push_back({front[i], sideN});
      vertices.push_back({front[j], sideN});
      vertices.push_back({back[j], sideN});
      vertices.push_back({back[i], sideN});

      indices.push_back(base);
      indices.push_back(base + 2);
      indices.push_back(base + 1);
      indices.push_back(base);
      indices.push_back(base + 3);
      indices.push_back(base + 2);
    }
  }

  return StlMesh::fromGeometry(std::move(vertices), std::move(indices));
}
