#pragma once

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
