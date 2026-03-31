# AGENTS

Instructions for AI coding agents working in this repository.

## Git Workflow Rules

- Never commit directly to `main`.
- Always create and work on a branch (`feature/...`, `fix/...`, `refactor/...`).
- Make small, focused commits with clear messages.
- Keep each commit scoped to one logical change whenever possible.
- Build and verify changes before opening a merge request or merging.
- Merge to `main` only after branch changes are complete and validated.

## Build Rules

- Always use Docker-based build scripts for build and verification steps.
- Build scripts are in `scripts/`:
	- `scripts/docker-build-debug.sh`
	- `scripts/docker-build-release.sh`
	- `scripts/docker-build-windows.sh`

## .nut File Format Rules

- Keep `.nut` file format documentation in `docs/nut-file-format.md` up to date
