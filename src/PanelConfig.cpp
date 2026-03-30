#include "PanelConfig.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace {
#ifdef _WIN32
#define CAMSTER_POPEN _popen
#define CAMSTER_PCLOSE _pclose
#else
#define CAMSTER_POPEN popen
#define CAMSTER_PCLOSE pclose
#endif

std::string escapeArg(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for (char c : value) {
    if (c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string runCommand(const std::string& cmd, int* exitCode) {
  std::array<char, 512> buffer{};
  std::string output;

  std::unique_ptr<FILE, int (*)(FILE*)> pipe(CAMSTER_POPEN(cmd.c_str(), "r"), CAMSTER_PCLOSE);
  if (!pipe) {
    if (exitCode) {
      *exitCode = -1;
    }
    return {};
  }

  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
    output.append(buffer.data());
  }

  if (exitCode) {
    *exitCode = CAMSTER_PCLOSE(pipe.release());
  }

  return output;
}
}  // namespace

PanelConfig PanelConfig::defaults() {
  PanelConfig cfg;
  cfg.buttons = {
      {"Open", "open", ""},
      {"Export", "export", ""},
      {"Wireframe", "toggle_wireframe", ""},
      {"Normals", "toggle_normals", ""},
      {"Snap Front", "snap", "front"},
      {"Snap Top", "snap", "top"},
      {"Snap ISO", "snap", "isometric"},
  };
  return cfg;
}

PanelConfig PanelConfig::loadFromPythonScript(const std::string& scriptPath, std::string& warning) {
  warning.clear();

  std::ostringstream cmd;
  cmd << escapeArg(CAMSTER_PYTHON_EXECUTABLE) << " " << escapeArg(scriptPath);

  int code = 0;
  std::string output = runCommand(cmd.str(), &code);
  if (code != 0 || output.empty()) {
    warning = "Panel script failed; using default panel.";
    return defaults();
  }

  try {
    const auto json = nlohmann::json::parse(output);
    PanelConfig cfg;

    if (!json.contains("buttons") || !json.at("buttons").is_array()) {
      warning = "Panel script missing 'buttons'; using default panel.";
      return defaults();
    }

    for (const auto& item : json.at("buttons")) {
      PanelButton b;
      b.label = item.value("label", "Unnamed");
      b.action = item.value("action", "");
      b.argument = item.value("argument", "");
      if (!b.action.empty()) {
        cfg.buttons.push_back(std::move(b));
      }
    }

    if (cfg.buttons.empty()) {
      warning = "Panel script produced no actions; using default panel.";
      return defaults();
    }

    return cfg;
  } catch (const std::exception&) {
    warning = "Panel script returned invalid JSON; using default panel.";
    return defaults();
  }
}
