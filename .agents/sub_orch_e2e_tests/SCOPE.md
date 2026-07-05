# Scope: Live Wallpaper E2E Testing Track

## Architecture
- The test suite is implemented as a standalone test runner (e.g. Python script `tests/e2e_test_runner.py` or similar).
- The test runner invokes `build/LiveWallpaper.exe` (or `LiveWallpaper.exe` in the build directories) in a separate process.
- The test runner configures the application state via `%APPDATA%\LiveWallpaper\config.ini`.
- The test runner verifies application behavior by checking:
  - Exit codes and standard output.
  - `%APPDATA%\LiveWallpaper\log.txt` contents (HRESULT codes, event flow, timestamps).
  - `%APPDATA%\LiveWallpaper\diagnostic_report.txt` contents (created during `--diagnose`).
  - Active Win32 window handles (Progman, WorkerW, and the injected host window).
  - Performance/idle/power state behavior.

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | Test Infrastructure Setup | Define the test plan in TEST_INFRA.md and prepare test runner framework | None | DONE |
| M2 | Feature and Edge Case Tests (Tiers 1 & 2) | Implement Tier 1 (5 features) and Tier 2 (boundaries/corners) tests | M1 | IN_PROGRESS (Conv: 650b27f3-2337-47ac-ad36-496048927406) |
| M3 | Combinations and Scenarios (Tiers 3 & 4) | Implement Tier 3 (cross-feature pairs) and Tier 4 (real-world workloads) | M2 | IN_PROGRESS (Conv: 650b27f3-2337-47ac-ad36-496048927406) |
| M4 | Verification & Audit | Execute tests, review code and behavior, run challenger verification and forensic auditing | M3 | PLANNED |
| M5 | Publish and Completion | Publish TEST_READY.md and report to parent | M4 | PLANNED |

## Interface Contracts
### Test Runner ↔ LiveWallpaper.exe
- CLI Interface: `--diagnose` flag runs without rendering and exits with code 0, producing `diagnostic_report.txt` and stdout report.
- Configuration: `%APPDATA%\LiveWallpaper\config.ini` controls video paths, playlist, pause, rotation intervals, idle timeouts.
- Logging: `%APPDATA%\LiveWallpaper\log.txt` contains structured logs of D3D11, MF, and WorkerW stages.
