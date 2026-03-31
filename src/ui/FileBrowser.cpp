#include "ui/FileBrowser.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#include <imgui.h>

namespace {

bool extensionMatches(const std::filesystem::path& path,
                      const std::vector<std::string>& exts) {
  if (exts.empty()) return true;
  // Case-insensitive comparison
  std::string ext = path.extension().string();
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  for (const auto& e : exts) {
    std::string le = e;
    for (char& c : le) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext == le) return true;
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------

void FileBrowser::show(std::vector<FileBrowserFilter> filters,
                       std::filesystem::path startDir,
                       std::string dialogTitle,
                       std::string actionLabel) {
  filters_ = std::move(filters);
  dialogTitle_ = std::move(dialogTitle);
  actionLabel_ = std::move(actionLabel);
  confirmed_ = false;
  selectedEntry_ = -1;
  selectedFilter_ = 0;
  filenameBuffer_[0] = '\0';
  selectedPath_.clear();

  if (startDir.empty() || !std::filesystem::exists(startDir)) {
    currentDir_ = std::filesystem::current_path();
  } else {
    currentDir_ = std::move(startDir);
  }

  refreshEntries();
  visible_ = true;
  pendingOpen_ = true;
}

// ---------------------------------------------------------------------------

bool FileBrowser::draw() {
  if (!visible_) return false;

  const std::string title = dialogTitle_ + "###" + kPopupId;

  if (pendingOpen_) {
    ImGui::OpenPopup(title.c_str());
    pendingOpen_ = false;
  }

  const ImGuiIO& io = ImGui::GetIO();
  const float width = std::min(600.0f, io.DisplaySize.x * 0.85f);
  const float height = std::min(480.0f, io.DisplaySize.y * 0.85f);

  ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
      ImGuiCond_Always, ImVec2(0.5f, 0.5f));

  bool done = false;
  bool requestClose = false;

  if (ImGui::BeginPopupModal(title.c_str(), nullptr,
                             ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove)) {
    // --- Current path + Up button ---
    ImGui::TextUnformatted(currentDir_.string().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Up")) {
      const auto parent = currentDir_.parent_path();
      if (parent != currentDir_) {
        currentDir_ = parent;
        selectedEntry_ = -1;
        filenameBuffer_[0] = '\0';
        refreshEntries();
      }
    }
    ImGui::Separator();

    // --- Entry list ---
    const float filterRowHeight =
        filters_.empty() ? 0.0f : ImGui::GetFrameHeightWithSpacing();
    const float bottomReserved =
        ImGui::GetFrameHeightWithSpacing() * 2.0f +  // filename + separator
        filterRowHeight +
        ImGui::GetFrameHeightWithSpacing() +  // action buttons
        ImGui::GetStyle().ItemSpacing.y * 4.0f;
    const float listHeight = height - ImGui::GetCursorPosY() - bottomReserved;

    if (ImGui::BeginChild("##fb_entries", ImVec2(0.0f, listHeight), true)) {
      for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const auto& entry = entries_[i];
        const bool selected = (i == selectedEntry_);
        ImGui::PushID(i);

        const std::string label =
            (entry.isDir ? "[/] " : "    ") + entry.name;

        if (ImGui::Selectable(label.c_str(), selected,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
          selectedEntry_ = i;

          if (!entry.isDir) {
            std::copy(entry.name.begin(), entry.name.end(), filenameBuffer_);
            filenameBuffer_[entry.name.size()] = '\0';
          }

          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (entry.isDir) {
              currentDir_ /= entry.name;
              selectedEntry_ = -1;
              filenameBuffer_[0] = '\0';
              refreshEntries();
            } else {
              selectedPath_ = currentDir_ / entry.name;
              confirmed_ = true;
              requestClose = true;
            }
          }
        }
        ImGui::PopID();
      }
    }
    ImGui::EndChild();

    // --- Filename input ---
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##fb_filename", filenameBuffer_, sizeof(filenameBuffer_));

    // --- Filter picker ---
    if (!filters_.empty()) {
      ImGui::SetNextItemWidth(-1.0f);
      const char* currentLabel = filters_[selectedFilter_].label.c_str();
      if (ImGui::BeginCombo("##fb_filter", currentLabel)) {
        for (int i = 0; i < static_cast<int>(filters_.size()); ++i) {
          const bool sel = (i == selectedFilter_);
          if (ImGui::Selectable(filters_[i].label.c_str(), sel)) {
            selectedFilter_ = i;
            refreshEntries();
          }
          if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }

    ImGui::Separator();

    // --- Action + Cancel buttons ---
    const float btnWidth =
        (ImGui::GetContentRegionAvail().x -
         ImGui::GetStyle().ItemSpacing.x) *
        0.5f;

    if (ImGui::Button(actionLabel_.c_str(), ImVec2(btnWidth, 0.0f))) {
      const std::string fname(filenameBuffer_);
      if (!fname.empty()) {
        selectedPath_ = currentDir_ / fname;
        confirmed_ = true;
        requestClose = true;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(btnWidth, 0.0f))) {
      confirmed_ = false;
      requestClose = true;
    }

    if (requestClose) {
      visible_ = false;
      done = true;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  return done;
}

// ---------------------------------------------------------------------------

void FileBrowser::refreshEntries() {
  entries_.clear();
  selectedEntry_ = -1;

  std::error_code ec;
  std::vector<Entry> dirs;
  std::vector<Entry> files;

  for (const auto& de :
       std::filesystem::directory_iterator(currentDir_, ec)) {
    if (ec) break;
    Entry e;
    e.isDir = de.is_directory(ec);
    e.name = de.path().filename().string();
    if (e.name.empty() || e.name.front() == '.') continue;  // skip hidden

    if (e.isDir) {
      dirs.push_back(std::move(e));
    } else if (matchesFilter(de.path())) {
      files.push_back(std::move(e));
    }
  }

  const auto byName = [](const Entry& a, const Entry& b) {
    return a.name < b.name;
  };
  std::sort(dirs.begin(), dirs.end(), byName);
  std::sort(files.begin(), files.end(), byName);

  entries_.insert(entries_.end(), std::make_move_iterator(dirs.begin()),
                  std::make_move_iterator(dirs.end()));
  entries_.insert(entries_.end(), std::make_move_iterator(files.begin()),
                  std::make_move_iterator(files.end()));
}

bool FileBrowser::matchesFilter(const std::filesystem::path& path) const {
  if (filters_.empty()) return true;
  if (selectedFilter_ < 0 ||
      selectedFilter_ >= static_cast<int>(filters_.size()))
    return true;
  return extensionMatches(path, filters_[selectedFilter_].extensions);
}
