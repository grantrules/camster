#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Persists 3D-print–related preferences: the ordered list of recently used
// slicer executables (most recent first).
struct PrintSettings {
  static constexpr int kMaxRecent = 10;

  std::vector<std::string> recentSlicers;  // most recent first

  // Prepend path to recentSlicers, deduplicating and capping at kMaxRecent.
  void addRecent(const std::string& path);

  static std::filesystem::path configPath();
  bool save() const;
  bool load();
};
