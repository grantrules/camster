#include "sketch/Extrude.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/geometric.hpp>

namespace {

float cross2D(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

glm::vec3 rotateAroundAxis(glm::vec3 point, glm::vec3 axisOrigin,
                           glm::vec3 axisDir, float angleRad) {
  const glm::vec3 p = point - axisOrigin;
  const glm::vec3 k = glm::normalize(axisDir);
  const float c = std::cos(angleRad);
  const float s = std::sin(angleRad);
  const glm::vec3 rotated = p * c + glm::cross(k, p) * s + k * glm::dot(k, p) * (1.0f - c);
  return axisOrigin + rotated;
}

glm::vec3 planeAxisDirection(SketchPlane plane, int axisMode) {
  switch (plane) {
    case SketchPlane::XY:
      return axisMode == 0 ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    case SketchPlane::XZ:
      return axisMode == 0 ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
    case SketchPlane::YZ:
      return axisMode == 0 ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
  }
  return glm::vec3(0.0f, 0.0f, 1.0f);
}

glm::vec3 sweepDirectionFromAxisMode(int axisMode) {
  switch (axisMode) {
    case 0: return {1.0f, 0.0f, 0.0f};
    case 1: return {0.0f, 1.0f, 0.0f};
    default: return {0.0f, 0.0f, 1.0f};
  }
}

std::vector<glm::vec2> resampleClosedProfile(const std::vector<glm::vec2>& input, int samples) {
  std::vector<glm::vec2> poly = input;
  if (poly.size() > 1 && glm::length(poly.front() - poly.back()) < 0.001f) {
    poly.pop_back();
  }
  if (poly.size() < 3 || samples < 3) return poly;

  std::vector<float> lengths(poly.size() + 1, 0.0f);
  for (size_t i = 0; i < poly.size(); ++i) {
    const size_t j = (i + 1) % poly.size();
    lengths[i + 1] = lengths[i] + glm::length(poly[j] - poly[i]);
  }
  const float total = lengths.back();
  if (total <= 1e-6f) return poly;

  std::vector<glm::vec2> out;
  out.reserve(samples + 1);
  for (int s = 0; s < samples; ++s) {
    const float target = total * static_cast<float>(s) / static_cast<float>(samples);
    size_t seg = 0;
    while (seg + 1 < lengths.size() && lengths[seg + 1] < target) ++seg;
    const size_t next = (seg + 1) % poly.size();
    const float segLen = lengths[seg + 1] - lengths[seg];
    const float t = segLen > 1e-6f ? (target - lengths[seg]) / segLen : 0.0f;
    out.push_back(glm::mix(poly[seg], poly[next], t));
  }
  out.push_back(out.front());
  return out;
}

void appendTriangle(std::vector<StlVertex>& vertices, std::vector<uint32_t>& indices,
                    glm::vec3 a, glm::vec3 b, glm::vec3 c) {
  glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
  if (glm::dot(n, n) < 1e-8f) n = {0.0f, 0.0f, 1.0f};
  const uint32_t base = static_cast<uint32_t>(vertices.size());
  vertices.push_back({a, n});
  vertices.push_back({b, n});
  vertices.push_back({c, n});
  indices.push_back(base);
  indices.push_back(base + 1);
  indices.push_back(base + 2);
}

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

StlMesh revolveMesh(const std::vector<std::vector<glm::vec2>>& profiles,
                    SketchPlane plane, float planeOffsetMm,
                    int axisMode, float angleDegrees, int segments) {
  if (profiles.empty() || std::abs(angleDegrees) < 1e-4f || segments < 3) return {};
  constexpr float kPiRevolve = 3.14159265358979f;
  const float angleRad = angleDegrees * kPiRevolve / 180.0f;
  const glm::vec3 axisOrigin = toWorld(glm::vec2(0.0f), plane, planeOffsetMm);
  const glm::vec3 axisDir = planeAxisDirection(plane, axisMode);

  std::vector<StlVertex> vertices;
  std::vector<uint32_t> indices;
  for (const auto& profile : profiles) {
    auto ring2d = resampleClosedProfile(profile, 64);
    if (ring2d.size() < 4) continue;
    ring2d.pop_back();
    const size_t ringSize = ring2d.size();

    std::vector<std::vector<glm::vec3>> rings(segments + 1, std::vector<glm::vec3>(ringSize));
    for (int s = 0; s <= segments; ++s) {
      const float t = static_cast<float>(s) / static_cast<float>(segments);
      const float step = angleRad * t;
      for (size_t i = 0; i < ringSize; ++i) {
        const glm::vec3 base = toWorld(ring2d[i], plane, planeOffsetMm);
        rings[s][i] = rotateAroundAxis(base, axisOrigin, axisDir, step);
      }
    }

    for (int s = 0; s < segments; ++s) {
      for (size_t i = 0; i < ringSize; ++i) {
        const size_t j = (i + 1) % ringSize;
        appendTriangle(vertices, indices, rings[s][i], rings[s][j], rings[s + 1][j]);
        appendTriangle(vertices, indices, rings[s][i], rings[s + 1][j], rings[s + 1][i]);
      }
    }
  }

  return StlMesh::fromGeometry(std::move(vertices), std::move(indices));
}

StlMesh sweepMesh(const std::vector<std::vector<glm::vec2>>& profiles,
                  SketchPlane plane, float planeOffsetMm,
                  int axisMode, float distanceMm) {
  const glm::vec3 dir = sweepDirectionFromAxisMode(axisMode) * distanceMm;
  if (profiles.empty() || glm::length(dir) < 1e-6f) return {};

  std::vector<StlVertex> vertices;
  std::vector<uint32_t> indices;
  for (const auto& profile : profiles) {
    auto ring = resampleClosedProfile(profile, 64);
    if (ring.size() < 4) continue;
    ring.pop_back();
    const size_t n = ring.size();
    std::vector<glm::vec3> front(n), back(n);
    for (size_t i = 0; i < n; ++i) {
      front[i] = toWorld(ring[i], plane, planeOffsetMm);
      back[i] = front[i] + dir;
    }
    const auto tris = earClip(profile);
    for (const auto& tri : tris) {
      appendTriangle(vertices, indices, toWorld(profile[tri[2]], plane, planeOffsetMm),
                     toWorld(profile[tri[1]], plane, planeOffsetMm),
                     toWorld(profile[tri[0]], plane, planeOffsetMm));
      appendTriangle(vertices, indices, toWorld(profile[tri[0]], plane, planeOffsetMm) + dir,
                     toWorld(profile[tri[1]], plane, planeOffsetMm) + dir,
                     toWorld(profile[tri[2]], plane, planeOffsetMm) + dir);
    }
    for (size_t i = 0; i < n; ++i) {
      const size_t j = (i + 1) % n;
      appendTriangle(vertices, indices, front[i], front[j], back[j]);
      appendTriangle(vertices, indices, front[i], back[j], back[i]);
    }
  }
  return StlMesh::fromGeometry(std::move(vertices), std::move(indices));
}

StlMesh loftMesh(const std::vector<std::vector<glm::vec2>>& profilesA,
                 SketchPlane planeA, float offsetA,
                 const std::vector<std::vector<glm::vec2>>& profilesB,
                 SketchPlane planeB, float offsetB,
                 int samples) {
  if (profilesA.empty() || profilesB.empty()) return {};
  auto a = resampleClosedProfile(profilesA.front(), samples);
  auto b = resampleClosedProfile(profilesB.front(), samples);
  if (a.size() < 4 || b.size() < 4) return {};
  a.pop_back();
  b.pop_back();
  const size_t n = std::min(a.size(), b.size());

  std::vector<StlVertex> vertices;
  std::vector<uint32_t> indices;
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    const glm::vec3 a0 = toWorld(a[i], planeA, offsetA);
    const glm::vec3 a1 = toWorld(a[j], planeA, offsetA);
    const glm::vec3 b1 = toWorld(b[j], planeB, offsetB);
    const glm::vec3 b0 = toWorld(b[i], planeB, offsetB);
    appendTriangle(vertices, indices, a0, a1, b1);
    appendTriangle(vertices, indices, a0, b1, b0);
  }
  const auto capA = earClip(a);
  for (const auto& tri : capA) {
    appendTriangle(vertices, indices, toWorld(a[tri[2]], planeA, offsetA),
                   toWorld(a[tri[1]], planeA, offsetA), toWorld(a[tri[0]], planeA, offsetA));
  }
  const auto capB = earClip(b);
  for (const auto& tri : capB) {
    appendTriangle(vertices, indices, toWorld(b[tri[0]], planeB, offsetB),
                   toWorld(b[tri[1]], planeB, offsetB), toWorld(b[tri[2]], planeB, offsetB));
  }
  return StlMesh::fromGeometry(std::move(vertices), std::move(indices));
}

StlMesh shellMesh(const StlMesh& mesh, float thicknessMm) {
  if (mesh.empty() || std::abs(thicknessMm) < 1e-4f) return {};
  auto outer = mesh.vertices();
  auto inner = mesh.vertices();
  const auto& inds = mesh.indices();
  for (auto& v : inner) {
    v.position -= v.normal * thicknessMm;
    v.normal = -v.normal;
  }
  std::vector<StlVertex> verts;
  std::vector<uint32_t> outInds;
  verts.reserve(outer.size() + inner.size());
  outInds.reserve(inds.size() * 2);
  verts.insert(verts.end(), outer.begin(), outer.end());
  verts.insert(verts.end(), inner.begin(), inner.end());
  outInds.insert(outInds.end(), inds.begin(), inds.end());
  const uint32_t innerBase = static_cast<uint32_t>(outer.size());
  for (size_t i = 0; i + 2 < inds.size(); i += 3) {
    outInds.push_back(innerBase + inds[i + 0]);
    outInds.push_back(innerBase + inds[i + 2]);
    outInds.push_back(innerBase + inds[i + 1]);
  }
  return StlMesh::fromGeometry(std::move(verts), std::move(outInds));
}
