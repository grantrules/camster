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
#include <string>

constexpr uint32_t CAMSTER_NUT_FORMAT_VERSION = 1;
constexpr uint32_t CAMSTER_NUT_MIN_READABLE_FORMAT_VERSION = 1;

constexpr const char* CAMSTER_NUT_METADATA_PATH = "metadata.json";
constexpr const char* CAMSTER_NUT_STATE_PATH = "state.json";
constexpr const char* CAMSTER_NUT_HISTORY_PATH = "history.json";
constexpr const char* CAMSTER_NUT_THUMBNAIL_PATH = "thumbnail.png";

struct NutMetadata {
	uint32_t formatVersion = CAMSTER_NUT_FORMAT_VERSION;
	std::string app = "camster";
	std::string appVersion;
	std::string createdUtc;
	std::string updatedUtc;
	int timelineLength = 0;
	int activeTimelineIndex = -1;
	bool hasThumbnail = false;
};

// Returns true when the given format version can be loaded by this binary.
bool nutFormatVersionSupported(uint32_t formatVersion);

// Validates metadata fields after parsing and before opening the project.
// On failure, `error` contains a user-facing message.
bool validateNutMetadata(const NutMetadata& metadata, std::string& error);

// Best-effort migration hook for legacy metadata versions.
// Returns true if metadata is now readable by this binary.
bool migrateNutMetadataInPlace(NutMetadata& metadata, std::string& error);
