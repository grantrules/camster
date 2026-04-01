#include "NutFile.hpp"

#include <algorithm>

bool nutFormatVersionSupported(uint32_t formatVersion) {
  return formatVersion >= CAMSTER_NUT_MIN_READABLE_FORMAT_VERSION &&
         formatVersion <= CAMSTER_NUT_FORMAT_VERSION;
}

bool validateNutMetadata(const NutMetadata& metadata, std::string& error) {
  if (!nutFormatVersionSupported(metadata.formatVersion)) {
    error = "Unsupported .nut format version";
    return false;
  }

  if (metadata.timelineLength < 0) {
    error = "Invalid metadata: timelineLength must be non-negative";
    return false;
  }

  if (metadata.activeTimelineIndex >= metadata.timelineLength && metadata.timelineLength > 0) {
    error = "Invalid metadata: activeTimelineIndex is outside timelineLength";
    return false;
  }

  if (metadata.activeTimelineIndex < -1) {
    error = "Invalid metadata: activeTimelineIndex must be >= -1";
    return false;
  }

  return true;
}

bool migrateNutMetadataInPlace(NutMetadata& metadata, std::string& error) {
  // Legacy bootstrapping rule:
  // formatVersion=0 indicates pre-versioned metadata; treat as v1 and continue.
  if (metadata.formatVersion == 0) {
    metadata.formatVersion = CAMSTER_NUT_FORMAT_VERSION;
  }

  if (!nutFormatVersionSupported(metadata.formatVersion)) {
    error = "No migration path for .nut format version";
    return false;
  }

  metadata.timelineLength = std::max(0, metadata.timelineLength);
  if (metadata.activeTimelineIndex >= metadata.timelineLength) {
    metadata.activeTimelineIndex = metadata.timelineLength > 0 ? metadata.timelineLength - 1 : -1;
  }
  metadata.activeTimelineIndex = std::max(-1, metadata.activeTimelineIndex);

  return validateNutMetadata(metadata, error);
}
