#include "ui/PrintWindow.hpp"

#include <cstdio>
#include <filesystem>

#include <imgui.h>

void PrintWindow::show(int objectCount, const PrintSettings& settings) {
  visible_ = true;
  browseRequested_ = false;
  recentComboIdx_ = 0;
  selected_.assign(static_cast<size_t>(objectCount), 1);
  slicerPath_ = settings.recentSlicers.empty() ? "" : settings.recentSlicers[0];
}

bool PrintWindow::draw(const std::vector<StlMesh>& objects,
                       const PrintSettings& settings) {
  if (!visible_) return false;

  // Keep checkbox list in sync if objects were added/removed while open.
  if (selected_.size() != objects.size()) {
    selected_.assign(objects.size(), 1);
  }

  bool printRequested = false;

  ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("3D Print", &visible_)) {
    // ------------------------------------------------------------------
    // Objects section
    // ------------------------------------------------------------------
    ImGui::TextUnformatted("Objects to export:");
    ImGui::Spacing();

    bool anySelected = false;
    for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
      char label[80];
      const unsigned tris = static_cast<unsigned>(objects[i].indices().size() / 3);
      std::snprintf(label, sizeof(label), "Object %d  (%u triangles)", i + 1, tris);
      bool v = selected_[i] != 0;
      ImGui::Checkbox(label, &v);
      selected_[i] = v ? 1 : 0;
      if (v) anySelected = true;
    }

    if (objects.empty()) {
      ImGui::TextDisabled("(no objects in scene)");
    }

    ImGui::Separator();

    // ------------------------------------------------------------------
    // Slicer section
    // ------------------------------------------------------------------
    ImGui::TextUnformatted("Slicer executable:");
    ImGui::Spacing();

    // Show filename prominently; full path as tooltip.
    if (slicerPath_.empty()) {
      ImGui::TextDisabled("(none selected)");
    } else {
      const std::string fname =
          std::filesystem::path(slicerPath_).filename().string();
      ImGui::TextUnformatted(fname.c_str());
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", slicerPath_.c_str());
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
      browseRequested_ = true;
    }

    // Recent slicers combo.
    if (!settings.recentSlicers.empty()) {
      ImGui::SameLine();
      ImGui::TextUnformatted("Recent:");
      ImGui::SameLine();

      if (recentComboIdx_ >= static_cast<int>(settings.recentSlicers.size()))
        recentComboIdx_ = 0;

      const auto getter = [](void* data, int idx, const char** out) -> bool {
        const auto* vec = static_cast<const std::vector<std::string>*>(data);
        if (idx < 0 || idx >= static_cast<int>(vec->size())) return false;
        // Show just the filename in the list for readability.
        *out = (*vec)[idx].c_str();
        return true;
      };

      ImGui::SetNextItemWidth(200.0f);
      if (ImGui::Combo("##recent", &recentComboIdx_, getter,
                       static_cast<void*>(const_cast<std::vector<std::string>*>(
                           &settings.recentSlicers)),
                       static_cast<int>(settings.recentSlicers.size()))) {
        slicerPath_ = settings.recentSlicers[recentComboIdx_];
      }
    }

    ImGui::Separator();

    // ------------------------------------------------------------------
    // Action buttons
    // ------------------------------------------------------------------
    const bool canPrint = anySelected && !slicerPath_.empty();

    if (!canPrint) ImGui::BeginDisabled();
    if (ImGui::Button("Print")) {
      printRequested = true;
      visible_ = false;
    }
    if (!canPrint) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      visible_ = false;
    }
  }
  ImGui::End();

  return printRequested;
}
