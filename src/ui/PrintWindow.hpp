#pragma once

#include <string>
#include <vector>

#include "PrintSettings.hpp"
#include "StlMesh.hpp"

// Self-contained "3D Print" window.
//
// Usage pattern in the render loop:
//   // Open:
//   app.printWindow.show(app.sceneObjects.size(), app.printSettings);
//
//   // Every frame:
//   bool printNow = app.printWindow.draw(app.sceneObjects, app.printSettings);
//   if (app.printWindow.slicerBrowseRequested()) {
//       app.printWindow.consumeBrowseRequest();
//       app.slicerBrowser.show(...);
//   }
//   if (printNow) { /* export + launch using printWindow.selectedObjects() + currentSlicer() */ }
class PrintWindow {
 public:
  // Open the window. Initialises per-object checkboxes and the current
  // slicer path from the most recently used entry in settings.
  void show(int objectCount, const PrintSettings& settings);

  bool isVisible() const { return visible_; }

  // Render the window.  Returns true on the frame the user clicks Print.
  bool draw(const std::vector<StlMesh>& objects, const PrintSettings& settings);

  // Access selected state after draw() returns true.
  const std::vector<int>& selectedObjects() const { return selected_; }
  const std::string& currentSlicer() const { return slicerPath_; }

  // Called by main after a successful FileBrowser selection.
  void setSlicerPath(const std::string& path) {
    slicerPath_ = path;
    recentComboIdx_ = 0;
  }

  // Flag checked by main to open the slicer FileBrowser at top level.
  bool slicerBrowseRequested() const { return browseRequested_; }
  void consumeBrowseRequest() { browseRequested_ = false; }

 private:
  bool visible_ = false;
  std::vector<int> selected_;  // 1 = selected, 0 = deselected
  std::string slicerPath_;
  bool browseRequested_ = false;
  int recentComboIdx_ = 0;
};
