# Plan: Live Wallpaper Target Machine Fixes

This plan outlines the milestones, roles, and verification steps required to implement the requirements (R1 through R5) for the Live Wallpaper project. Due to resource constraints (code 429), nested sub-orchestrators have been bypassed. The Project Orchestrator will directly coordinate flat worker and reviewer tasks.

## Milestone Decomposition

### Milestone 1: E2E Test Suite Development (Tiers 1-4)
- **Objective**: Design and implement the opaque-box E2E test suite in Python. The test suite will verify command line options, config file parsing, log creation, and the diagnostic tool report.
- **Verification**: Run the Python script and verify expected failure patterns on current codebase.
- **Status**: PLANNED

### Milestone 2: Implementation of R1, R2, R3, R4, R5
- **Objective**: Implement all requested features:
  - R1: Render pipeline diagnostic logging in all key files.
  - R2: Hardened WorkerW desktop window attachment and fallback discovery.
  - R3: Direct3D11 creation hardening, WARP check, NV12 support check, swap effect fallback.
  - R4: Source reader resilience, decoder stall logging, frame flows.
  - R5: Machine environment diagnostic CLI tool.
- **Verification**: Code compiles, unit tests pass, and newly written E2E tests pass.
- **Status**: PLANNED

### Milestone 3: Review & Hardening (Phase 2 / Tier 5)
- **Objective**: Conduct peer review, run Challenger test stress-tests, run Forensic Auditor to verify integrity, and fix any bugs found.
- **Status**: PLANNED

## Team Roles & Specialist Assignments
We will spawn:
1. **Worker (`teamwork_preview_worker`)** for Milestone 1 (E2E Test Suite).
2. **Worker (`teamwork_preview_worker`)** for Milestone 2 (Implementation of R1-R5).
3. **Reviewers / Challengers / Auditor** for Milestone 3 (Verification & Gating).

## Global Verification Strategy
- **Compiler**: CMake + MSVC toolchain (`cmake --build build`) must pass without errors.
- **Unit Tests**: Existing unit tests in `LiveWallpaperTests` must pass.
- **E2E Tests**: Python-based E2E tests must pass 100%.
