#include "PrintSettings.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

// Defined in VulkanRenderer.cpp — returns the directory containing the
// running executable.
std::filesystem::path executableDir();

std::filesystem::path PrintSettings::configPath() {
  return executableDir() / "camster_print.ini";
}

void PrintSettings::addRecent(const std::string& path) {
  if (path.empty()) return;
  // Remove existing entry with the same path.
  recentSlicers.erase(std::remove(recentSlicers.begin(), recentSlicers.end(), path),
                      recentSlicers.end());
  recentSlicers.insert(recentSlicers.begin(), path);
  if (static_cast<int>(recentSlicers.size()) > kMaxRecent)
    recentSlicers.resize(kMaxRecent);
}

bool PrintSettings::save() const {
  std::ofstream out(configPath());
  if (!out) return false;
  for (int i = 0; i < static_cast<int>(recentSlicers.size()); ++i) {
    out << "recent" << i << "=" << recentSlicers[i] << "\n";
  }
  return out.good();
}

bool PrintSettings::load() {
  std::ifstream in(configPath());
  if (!in) return false;

  recentSlicers.clear();

  // Collect (index, path) pairs so we can sort by index and handle gaps.
  std::vector<std::pair<int, std::string>> items;
  std::string line;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);
    if (val.empty()) continue;
    if (key.size() > 6 && key.substr(0, 6) == "recent") {
      try {
        int idx = std::stoi(key.substr(6));
        items.emplace_back(idx, val);
      } catch (const std::exception&) {}
    }
  }

  std::sort(items.begin(), items.end());
  for (auto& [idx, val] : items) {
    recentSlicers.push_back(val);
  }

  if (static_cast<int>(recentSlicers.size()) > kMaxRecent)
    recentSlicers.resize(kMaxRecent);

  return true;
}
