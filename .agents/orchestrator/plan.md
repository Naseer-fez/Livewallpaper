# Plan: Live Wallpaper Engine Follow-up Fixes

This plan outlines the milestones, roles, and verification steps required to resolve the memory leaks, thread safety issues, window recreation crashes, resize polling, and Rust FFI security issues (Requirements R1 through R5 from follow-up).

## Milestone Decomposition

### Milestone 1: Codebase Exploration & Fix Mapping
- **Objective**: Spawn `teamwork_preview_explorer` to analyze the source code and identify the exact locations and required structures to resolve:
  - PathMessage queue insertion failures in `SynchronizationManager`.
  - IMFSample COM reference leaks in `VideoDecoder`.
  - Destructors for lock-free collections/managers.
  - Thread-safety for `ID3D11DeviceContext` and texture reallocations.
  - Safe window destruction/recreation and exponential backoff in `ExplorerIntegration`.
  - Cache size polling and pacing in `main.cpp`/`RenderThreadController`.
  - Propagating Rust shader compile errors and securing DLL loading.
- **Verification**: Explorer produces an exploration/analysis report with exact file/line mapping and pseudocode/fix strategy.
- **Status**: PLANNED

### Milestone 2: Implementation of Fixes (R1-R5)
- **Objective**: Spawn `teamwork_preview_worker` to write the C++ and Rust modifications, compile using the build script, and check that unit and E2E tests run and pass.
- **Verification**: Compilation succeeds without warnings, and C++ unit tests + python E2E tests pass.
- **Status**: PLANNED

### Milestone 3: Review, Challenge & Forensic Verification
- **Objective**:
  - Spawn `teamwork_preview_reviewer` to check for concurrency/resource safety, ensuring there are no race conditions on context access.
  - Spawn `teamwork_preview_challenger` to stress-test the window recreation, resizing, and error handling.
  - Spawn `teamwork_preview_auditor` to conduct the Forensic Audit verifying that no cheating occurred and code layout is correct.
- **Verification**: Reviewer passes, Challenger reports success under stress, and Auditor attests cleanliness.
- **Status**: PLANNED

## Team Roles & Specialist Assignments
1. **Explorer (`teamwork_preview_explorer`)**: Investigates the codebase and writes the fix strategy.
2. **Worker (`teamwork_preview_worker`)**: Implements the fixes, compiles, runs tests.
3. **Reviewers / Challengers / Auditor**: Verification and validation.
