# Camster .nut File Format Specification

This document is the source of truth for the Camster project file format.

Status: Draft
Current format version: 1
File extension: `.nut`
Container: ZIP archive

## 1. Goals

The `.nut` format is designed to be:

- Easy to parse with simple tooling
- Friendly to long-term versioning
- Fast to load for normal usage
- Fully replayable for deterministic rebuilds

## 2. Container Layout

A `.nut` file is a ZIP archive with these required entries:

- `metadata.json`
- `state.json`
- `history.json`

Optional entries:

- `thumbnail.png`
- `assets/` (future external resources)

## 3. Required Files

### 3.1 metadata.json

`metadata.json` contains global project metadata and format-level information.

Required fields:

- `formatVersion` (integer): must be present; identifies the file schema version

Recommended fields:

- `app` (string): writing application name, e.g. `camster`
- `appVersion` (string): app version/build identifier
- `createdUtc` (string): ISO-8601 timestamp
- `updatedUtc` (string): ISO-8601 timestamp
- `timelineLength` (integer): number of entries in `history.json`
- `activeTimelineIndex` (integer): current timeline index when saved
- `hasThumbnail` (boolean): whether `thumbnail.png` exists

Example:

```json
{
  "formatVersion": 1,
  "app": "camster",
  "appVersion": "0.1.0",
  "createdUtc": "2026-03-31T18:22:11Z",
  "updatedUtc": "2026-03-31T18:29:43Z",
  "timelineLength": 42,
  "activeTimelineIndex": 41,
  "hasThumbnail": true
}
```

### 3.2 state.json

`state.json` stores a complete current state snapshot for fast load.

It should contain enough data to load the project without replaying full history.

At minimum it includes:

- Sketch list and sketch metadata
- Sketch geometry and constraints
- Scene object list and object metadata
- Tool/project settings needed for immediate editing
- Current timeline position

### 3.3 history.json

`history.json` stores all timeline entries in order.

This is the canonical replay log used to rebuild project state from an empty scene.

Each entry includes:

- Stable entry id
- Action type
- Action payload
- Optional conflict marker/message

History should be append-only during normal editing; edits from a past timeline point may truncate and rewrite entries after that point.

## 4. Thumbnail

If present, `thumbnail.png` is a small PNG preview image of the current scene.

Guidelines:

- Format: PNG
- Recommended size: 256x256 or 512x512
- Color space: sRGB
- Include alpha channel if convenient

The thumbnail is optional. Readers must tolerate missing thumbnails.

## 5. Versioning Rules

`formatVersion` is mandatory and must follow these rules:

- Increment for any breaking schema change
- Do not reuse old version numbers for different schemas
- Older readers may refuse unknown versions
- Newer readers should attempt compatibility where possible and surface clear errors

Breaking change examples:

- Removing or renaming required fields
- Changing field semantics incompatibly
- Changing required file paths in the ZIP layout

Non-breaking change examples:

- Adding optional fields
- Adding optional files
- Adding new action types that older readers can safely ignore (if designed for it)

## 6. Source-of-Truth Policy

Any change to `.nut` schema or semantics must update this document in the same change.

If the change is breaking, it must also:

- Bump `formatVersion`
- Add a new changelog entry at the bottom of this file
- Update all relevant serializer/deserializer comments and constants in source

## 7. Changelog

### v1

- Initial ZIP-based `.nut` format defined
- Required files: `metadata.json`, `state.json`, `history.json`
- Optional thumbnail support via `thumbnail.png`
- Mandatory `formatVersion` field in metadata
