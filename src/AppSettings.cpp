#include "AppSettings.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>

// Defined in VulkanRenderer.cpp — returns the directory containing the executable.
std::filesystem::path executableDir();

std::filesystem::path AppSettings::configPath() {
  return executableDir() / "camster_settings.ini";
}

bool AppSettings::save() const {
  std::ofstream out(configPath());
  if (!out) return false;

  out << "defaultUnit=" << static_cast<int>(defaultUnit) << "\n";
  out << "gridSpacing=" << gridSpacing << "\n";
  out << "gridExtent=" << gridExtent << "\n";
  out << "exportUnit=" << static_cast<int>(exportUnit) << "\n";

  return out.good();
}

bool AppSettings::load() {
  std::ifstream in(configPath());
  if (!in) return false;

  std::string line;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);

    if (key == "defaultUnit") {
      int v = std::stoi(val);
      if (v >= 0 && v < kUnitCount) defaultUnit = static_cast<Unit>(v);
    } else if (key == "gridSpacing") {
      gridSpacing = std::stof(val);
    } else if (key == "gridExtent") {
      gridExtent = std::stof(val);
    } else if (key == "exportUnit") {
      int v = std::stoi(val);
      if (v >= 0 && v < kUnitCount) exportUnit = static_cast<Unit>(v);
    }
  }
  return true;
}
