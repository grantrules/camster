#pragma once

// .nut file format constants and policy.
//
// IMPORTANT MAINTENANCE RULE:
// If you change the .nut on-disk schema or semantics, you MUST:
// 1) update docs/nut-file-format.md in the same commit,
// 2) bump CAMSTER_NUT_FORMAT_VERSION for any breaking change, and
// 3) append an entry to the changelog section at the bottom of that spec.
//
// Keep the specification and implementation in lock-step.

#include <cstdint>

constexpr uint32_t CAMSTER_NUT_FORMAT_VERSION = 1;

constexpr const char* CAMSTER_NUT_METADATA_PATH = "metadata.json";
constexpr const char* CAMSTER_NUT_STATE_PATH = "state.json";
constexpr const char* CAMSTER_NUT_HISTORY_PATH = "history.json";
constexpr const char* CAMSTER_NUT_THUMBNAIL_PATH = "thumbnail.png";
