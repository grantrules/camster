#pragma once

// History / timeline data model.
// Every significant action recorded while editing a project is stored as a
// HistoryEntry.  The complete sequence of entries can be replayed from
// scratch to reconstruct the project state at any point in time.

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <glm/vec2.hpp>

#include "ProjectTypes.hpp"
#include "Scene.hpp"
#include "sketch/Constraint.hpp"
#include "sketch/Primitive.hpp"

// ---- Action payloads ------------------------------------------------

struct CreatePlaneAction {
  int planeId = -1;
  PlaneReference reference;
  std::string name;
};

// A new sketch was created.
struct CreateSketchAction {
  int planeId = -1;
  std::string name;
};

// Snapshot of a sketch taken when the user exits sketch mode.
// Stored elements may contain SketchElementSource references that allow the
// timeline to re-project geometry when an upstream sketch changes.
struct EditSketchAction {
  int sketchIndex = -1;
  std::string sketchName;
  std::vector<SketchElement> elements;
  std::vector<SketchConstraint> constraints;
};

// An extrude operation that was applied.
struct ExtrudeAction {
  int sketchIndex = -1;
  std::string sketchName;
  // Profiles are the tessellated closed loops from the sketch at the time.
  std::vector<std::vector<glm::vec2>> profiles;
  SketchPlane plane = SketchPlane::XY;
  float sketchOffsetMm = 0.0f;
  float depthMm = 0.0f;
  bool subtract = false;
  // Object names are used to find targets by identity during replay.
  std::vector<std::string> targetObjectNames;
  std::string resultObjectName;
};

// A combine merge / subtract operation.
struct CombineAction {
  bool subtract = false;
  bool keepTools = false;
  std::vector<std::string> targetNames;
  std::vector<std::string> toolNames;
  std::string resultObjectName;
};

using HistoryAction = std::variant<
  CreatePlaneAction,
    CreateSketchAction,
    EditSketchAction,
    ExtrudeAction,
    CombineAction>;

// ---- Timeline entry -------------------------------------------------

struct HistoryEntry {
  uint64_t id = 0;
  std::string displayName;
  HistoryAction action;
  bool hasConflict = false;
};

// ---- Timeline -------------------------------------------------------

class Timeline {
 public:
  // Append a new entry.  Returns the id assigned to it.
  uint64_t push(HistoryAction action, const std::string& displayName);

  // Overwrite the payload of an existing entry (identified by id).
  bool updateEntry(uint64_t id, HistoryAction action);

  // Mark / clear the conflict flag on an entry.
  void markConflict(uint64_t id, bool conflict);

  // Remove all entries from index `from` onward (0-based).
  void truncateFrom(int from);

  // Clear conflict flags from `from` onward.
  void clearConflictsFrom(int from);

  const std::vector<HistoryEntry>& entries() const { return entries_; }
  std::vector<HistoryEntry>& entries() { return entries_; }
  int size() const { return static_cast<int>(entries_.size()); }
  bool empty() const { return entries_.empty(); }
  void clear() { entries_.clear(); }

  // Find entry by id; returns nullptr if not found.
  HistoryEntry* find(uint64_t id);
  const HistoryEntry* find(uint64_t id) const;

 private:
  std::vector<HistoryEntry> entries_;
  uint64_t nextId_ = 1;
};
