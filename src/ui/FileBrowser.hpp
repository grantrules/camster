#pragma once

#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// FileBrowserFilter
//
// A named filter shown in the filter drop-down.
//   label      – display name, e.g. "STL Files"
//   extensions – lowercase extensions including the dot, e.g. {".stl", ".obj"}
//                Empty extensions list means "accept all".
// ---------------------------------------------------------------------------
struct FileBrowserFilter {
  std::string label;
  std::vector<std::string> extensions;
};

// ---------------------------------------------------------------------------
// FileBrowser
//
// A self-contained ImGui modal file browser.  Designed to be embedded as a
// value member (e.g. inside AppState) and driven by the main render loop.
//
// Usage:
//   // somewhere in response to user input:
//   fileBrowser.show({{"STL Files", {".stl"}}});
//
//   // every frame while the dialog is open:
//   if (fileBrowser.isVisible()) {
//     if (fileBrowser.draw() && fileBrowser.confirmed()) {
//       handlePath(fileBrowser.selectedPath());
//     }
//   }
// ---------------------------------------------------------------------------
class FileBrowser {
 public:
  // Open the dialog. startDir defaults to the current working directory.
  // dialogTitle / actionLabel allow future operations (Save/Import/Export)
  // without changing this class API.
  void show(std::vector<FileBrowserFilter> filters = {},
            std::filesystem::path startDir = {},
            std::string dialogTitle = "Open File",
            std::string actionLabel = "Open");

  bool isVisible() const { return visible_; }

  // Call every frame while isVisible().
  // Returns true when the dialog closes (confirm or cancel).
  bool draw();

  // Valid only after draw() returns true.
  bool confirmed() const { return confirmed_; }
  const std::filesystem::path& selectedPath() const { return selectedPath_; }

 private:
  struct Entry {
    std::string name;
    bool isDir = false;
  };

  void refreshEntries();
  bool matchesFilter(const std::filesystem::path& path) const;
  const char* actionLabel() const;

  bool visible_ = false;
  bool pendingOpen_ = false;
  bool confirmed_ = false;

  std::vector<FileBrowserFilter> filters_;
  int selectedFilter_ = 0;

  std::string dialogTitle_ = "Open File";
  std::string actionLabel_ = "Open";

  std::filesystem::path currentDir_;
  std::filesystem::path selectedPath_;

  char filenameBuffer_[512] = {};
  int selectedEntry_ = -1;
  std::vector<Entry> entries_;

  static constexpr const char* kPopupId = "camster_file_browser";
};
