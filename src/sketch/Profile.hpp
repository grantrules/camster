#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/vec2.hpp>

#include "sketch/Primitive.hpp"

// Tessellates sketch primitives into 2D polylines and chains them into
// closed loops suitable for extrusion.
namespace profile {

constexpr int kSegments = 64;
constexpr float kPi = 3.14159265358979f;

inline float cross2D(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

inline bool pointInTriangle(glm::vec2 pt, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
  const float d1 = cross2D(b - a, pt - a);
  const float d2 = cross2D(c - b, pt - b);
  const float d3 = cross2D(a - c, pt - c);
  const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
  const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
  return !(hasNeg && hasPos);
}

// Convert a single primitive to an ordered sequence of 2D vertices.
// Closed shapes return a polyline where first ≈ last.
inline std::vector<glm::vec2> tessellate2D(const SketchPrimitive& prim) {
  return std::visit(
      [](const auto& p) -> std::vector<glm::vec2> {
        using T = std::decay_t<decltype(p)>;

        if constexpr (std::is_same_v<T, SketchLine>) {
          return {p.start, p.end};

        } else if constexpr (std::is_same_v<T, SketchRect>) {
          return {p.min, {p.max.x, p.min.y}, p.max, {p.min.x, p.max.y}, p.min};

        } else if constexpr (std::is_same_v<T, SketchCircle>) {
          std::vector<glm::vec2> pts;
          pts.reserve(kSegments + 1);
          for (int i = 0; i <= kSegments; ++i) {
            float a = 2.0f * kPi * static_cast<float>(i) / kSegments;
            pts.push_back(p.center + p.radius * glm::vec2(std::cos(a), std::sin(a)));
          }
          return pts;

        } else if constexpr (std::is_same_v<T, SketchArc>) {
          const int segs =
              std::max(4, static_cast<int>(std::abs(p.sweepAngle) / (2.0f * kPi) * kSegments));
          std::vector<glm::vec2> pts;
          pts.reserve(segs + 1);
          for (int i = 0; i <= segs; ++i) {
            float t = p.startAngle + p.sweepAngle * static_cast<float>(i) / segs;
            pts.push_back(p.center + p.radius * glm::vec2(std::cos(t), std::sin(t)));
          }
          return pts;
        }
      },
      prim);
}

inline bool isClosed(const std::vector<glm::vec2>& poly, float tol = 0.5f) {
  return poly.size() >= 3 && glm::length(poly.front() - poly.back()) < tol;
}

inline std::vector<std::array<size_t, 3>> triangulate2D(const std::vector<glm::vec2>& poly) {
  std::vector<std::array<size_t, 3>> tris;

  size_t n = poly.size();
  if (n > 0 && glm::length(poly.front() - poly.back()) < 0.001f) --n;
  if (n < 3) return tris;

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

      if (cross2D(poly[curr] - poly[prev], poly[next] - poly[curr]) <= 0.0f) continue;

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

    if (!earFound) break;
  }

  return tris;
}

// Given polylines from selected primitives, chain them into closed loops.
// Already-closed polylines pass through directly.  Open polylines are
// chained end-to-end when their endpoints are within `tolerance`.
inline std::vector<std::vector<glm::vec2>> chainProfiles(
    const std::vector<std::vector<glm::vec2>>& polylines, float tolerance = 0.5f) {
  std::vector<std::vector<glm::vec2>> result;

  std::vector<std::vector<glm::vec2>> open;
  for (const auto& pl : polylines) {
    if (pl.size() < 2) continue;
    if (isClosed(pl, tolerance)) {
      result.push_back(pl);
    } else {
      open.push_back(pl);
    }
  }

  std::vector<bool> used(open.size(), false);

  for (size_t i = 0; i < open.size(); ++i) {
    if (used[i]) continue;

    std::vector<glm::vec2> chain = open[i];
    used[i] = true;

    bool progress = true;
    while (progress) {
      progress = false;
      for (size_t j = 0; j < open.size(); ++j) {
        if (used[j]) continue;
        const auto& seg = open[j];

        if (glm::length(chain.back() - seg.front()) < tolerance) {
          chain.insert(chain.end(), seg.begin() + 1, seg.end());
          used[j] = true;
          progress = true;
        } else if (glm::length(chain.back() - seg.back()) < tolerance) {
          chain.insert(chain.end(), seg.rbegin() + 1, seg.rend());
          used[j] = true;
          progress = true;
        } else if (glm::length(chain.front() - seg.back()) < tolerance) {
          std::vector<glm::vec2> tmp(seg.begin(), seg.end() - 1);
          tmp.insert(tmp.end(), chain.begin(), chain.end());
          chain = std::move(tmp);
          used[j] = true;
          progress = true;
        } else if (glm::length(chain.front() - seg.front()) < tolerance) {
          std::vector<glm::vec2> tmp(seg.rbegin(), seg.rend() - 1);
          tmp.insert(tmp.end(), chain.begin(), chain.end());
          chain = std::move(tmp);
          used[j] = true;
          progress = true;
        }
      }
    }

    if (isClosed(chain, tolerance)) {
      result.push_back(std::move(chain));
    }
  }

  return result;
}

}  // namespace profile
