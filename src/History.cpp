#include "History.hpp"

#include <algorithm>

uint64_t Timeline::push(HistoryAction action, const std::string& displayName) {
  HistoryEntry entry;
  entry.id = nextId_++;
  entry.displayName = displayName;
  entry.action = std::move(action);
  entry.hasConflict = false;
  entries_.push_back(std::move(entry));
  return entries_.back().id;
}

bool Timeline::updateEntry(uint64_t id, HistoryAction action) {
  auto* e = find(id);
  if (!e) return false;
  e->action = std::move(action);
  e->hasConflict = false;
  return true;
}

void Timeline::markConflict(uint64_t id, bool conflict) {
  auto* e = find(id);
  if (e) e->hasConflict = conflict;
}

void Timeline::truncateFrom(int from) {
  if (from < 0) from = 0;
  if (from >= static_cast<int>(entries_.size())) return;
  entries_.erase(entries_.begin() + from, entries_.end());
}

void Timeline::clearConflictsFrom(int from) {
  for (int i = from; i < static_cast<int>(entries_.size()); ++i) {
    entries_[i].hasConflict = false;
  }
}

HistoryEntry* Timeline::find(uint64_t id) {
  for (auto& e : entries_) {
    if (e.id == id) return &e;
  }
  return nullptr;
}

const HistoryEntry* Timeline::find(uint64_t id) const {
  for (const auto& e : entries_) {
    if (e.id == id) return &e;
  }
  return nullptr;
}
