#pragma once

#include <string>
#include <vector>

struct PanelButton {
  std::string label;
  std::string action;
  std::string argument;
};

class PanelConfig {
 public:
  static PanelConfig loadFromPythonScript(const std::string& scriptPath, std::string& warning);
  static PanelConfig defaults();

  std::vector<PanelButton> buttons;
};
