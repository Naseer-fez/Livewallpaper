# E2E Test Infra: Live Wallpaper Engine

## Test Philosophy
- Opaque-box, requirement-driven. No dependency on implementation design.
- Methodology: Category-Partition + BVA + Pairwise + Workload Testing.

## Feature Inventory
| # | Feature | Source (requirement) | Tier 1 | Tier 2 | Tier 3 |
|---|---------|---------------------|:------:|:------:|:------:|
| 1 | Diagnostic logging | ORIGINAL_REQUEST §R1 | 5      | 5      | ✓      |
| 2 | WorkerW / Desktop | ORIGINAL_REQUEST §R2 | 5      | 5      | ✓      |
| 3 | D3D11 Hardening | ORIGINAL_REQUEST §R3 | 5      | 5      | ✓      |
| 4 | MF / Video Decoder | ORIGINAL_REQUEST §R4 | 5      | 5      | ✓      |
| 5 | Environment Diagnostic | ORIGINAL_REQUEST §R5 | 5      | 5      | ✓      |

## Test Architecture
- **Test Runner**: Python script (`tests/e2e_test_runner.py`) using subprocess to invoke compiled `LiveWallpaper.exe`.
- **Test Case Format**: Each test case is a Python unit test (`unittest` framework) that sets up specific prerequisites (e.g. config files, window states), executes the app or CLI commands, parses stdout and `log.txt`, and asserts the expected outcomes.
- **Directory Layout**:
  - `tests/e2e_test_runner.py` — Main entry point and implementation of all E2E test cases.
  - `%APPDATA%\LiveWallpaper\` — Location of `config.ini`, `log.txt`, `diagnostic_report.txt` used for verification.

---

## Test Cases Detailed Design

### Tier 1 — Feature Coverage (Happy Path)
#### Feature 1: Diagnostic logging
- **F1_T1_1**: Verification that diagnostic logging is initialized on startup. Checks that `log.txt` is created in `%APPDATA%\LiveWallpaper` and contains startup logs.
- **F1_T1_2**: Verification of log rotation works when size exceeds 1MB. Fills `log.txt` to >1MB, runs app, and checks for `log.bak` creation and truncation of `log.txt`.
- **F1_T1_3**: Verification that entry/exit and success/failure logging is printed for COM initialization (`CoInitializeEx` / `CoUninitialize`).
- **F1_T1_4**: Verification that first frame milestone is logged. Asserts "first frame milestone" or "first successfully decoded frame" matches in `log.txt`.
- **F1_T1_5**: Verification that log level filtering is respected (debug vs info levels).

#### Feature 2: WorkerW / Desktop Attachment Robustness
- **F2_T1_1**: WorkerW window hierarchy discovery. Verifies that the app successfully discovers the Progman or WorkerW handles.
- **F2_T1_2**: Verification of dedicated wallpaper WorkerW assignment. Asserts target HWND is assigned and logged.
- **F2_T1_3**: Injection confirmation: verifies that the host window is successfully parented under WorkerW and has child style.
- **F2_T1_4**: Watchdog recovery: kills and restarts shell/explorer window hierarchy or invalidates HWND, verifying auto-recovery is logged.
- **F2_T1_5**: Fallback to Progman when dedicated WorkerW is not found.

#### Feature 3: D3D11 Device Creation Hardening
- **F3_T1_1**: D3D11 hardware device creation. Confirms device and context are created successfully, logging feature level.
- **F3_T1_2**: Verify `CheckFormatSupport` for `DXGI_FORMAT_NV12` is executed and logged.
- **F3_T1_3**: Swap chain creation checks: verifies flip discard swap effect and back buffer dimensions.
- **F3_T1_4**: Verify `CreateRenderTargetView` succeeds and is non-null before rendering.
- **F3_T1_5**: Fallback to WARP software renderer when hardware D3D11 fails.

#### Feature 4: Media Foundation / Video Decoder Resilience
- **F4_T1_1**: DXGI Device Manager reset with D3D11 device check.
- **F4_T1_2**: Decoder fallback options sequence: tests fallback combinations on loading video.
- **F4_T1_3**: First successfully decoded frame logs its dimensions, format, and source path.
- **F4_T1_4**: Frame flow counter logs progress every 100 frames.
- **F4_T1_5**: Stream selection verification (disables non-video streams).

#### Feature 5: Machine Environment Diagnostic Tool
- **F5_T1_1**: `--diagnose` CLI execution. Confirms it runs without attempting rendering.
- **F5_T1_2**: Confirms report file `diagnostic_report.txt` is created in `%APPDATA%\LiveWallpaper`.
- **F5_T1_3**: Confirms report is written to stdout.
- **F5_T1_4**: Confirms report contains GPU details and OS version.
- **F5_T1_5**: Confirms report contains listed MF decoders.

---

### Tier 2 — Boundary & Corner Cases (>= 5 cases per feature)
#### Feature 1: Diagnostic logging
- **F1_T2_1**: App start when `%APPDATA%` dir doesn't exist (must auto-create).
- **F1_T2_2**: Parallel instances logs write contention (safety check for file sharing).
- **F1_T2_3**: Startup when `log.txt` is locked by another process (should not crash).
- **F1_T2_4**: Handling of extremely long format strings to avoid overflow.
- **F1_T2_5**: Wide character (non-ASCII) video paths logging.

#### Feature 2: WorkerW / Desktop Attachment Robustness
- **F2_T2_1**: Multiple WorkerW windows existing (must correctly find the wallpaper host WorkerW).
- **F2_T2_2**: Injected host window dimensions validation matching virtual screen sizes.
- **F2_T2_3**: Explorer crashes repeatedly in short succession (watchdog recovery rate-limiting).
- **F2_T2_4**: Progman window is completely missing (logs failure gracefully).
- **F2_T2_5**: WorkerW discovery when shell message hooks are blocked.

#### Feature 3: D3D11 Device Creation Hardening
- **F3_T2_1**: Device lost recovery state machine transitions.
- **F3_T2_2**: Swap chain effect fallback if `FLIP_DISCARD` is rejected.
- **F3_T2_3**: Formatting check on adapters without NV12 support.
- **F3_T2_4**: Swap chain resizing verification on resolution change.
- **F3_T2_5**: Fallback driver types validation (hardware -> WARP).

#### Feature 4: Media Foundation / Video Decoder Resilience
- **F4_T2_1**: Decoder stall detection (5 second timeout warning).
- **F4_T2_2**: Initialization failure of MF (graceful handling).
- **F4_T2_3**: Load corrupt or invalid video file path (safely handled by `ValidateFilePath` or `LoadVideo`).
- **F4_T2_4**: DXVA2 hardware decoding device recovery during playback.
- **F4_T2_5**: Buffer size verification on software decoding fallback path.

#### Feature 5: Machine Environment Diagnostic Tool
- **F5_T2_1**: Malformed/invalid command line flags (graceful fallback/usage info).
- **F5_T2_2**: Multiple monitors setup enumerations in diagnostic report.
- **F5_T2_3**: DWM disabled composition state detection.
- **F5_T2_4**: Empty/missing window handles enumeration.
- **F5_T2_5**: Run `--diagnose` when AppData directory is read-only.

---

### Tier 3 — Cross-Feature Combinations (Pairwise)
- **F_T3_1**: Explorer Integration watchdog recovery during active MF video playback (R2 + R4).
- **F_T3_2**: D3D11 device loss recovery during video decoding and presentation (R3 + R4).
- **F_T3_3**: Running `--diagnose` while normal `LiveWallpaper.exe` is active (R1 + R5).
- **F_T3_4**: Log rotation occurs while rendering video frame flow is active (R1 + R4).
- **F_T3_5**: DPI scaling/resolution change triggers swap chain re-creation (R2 + R3).

---

### Tier 4 — Real-World Application Scenarios (>= 5 scenarios)
- **F_T4_1**: **Full-Screen Auto-Pause/Resume**: Maximizing a window (e.g. Notepad) pauses rendering; restoring window resumes rendering.
- **F_T4_2**: **System Idle Timeout Auto-Pause/Resume**: Computer goes idle (no mouse/keyboard), rendering pauses; mouse moves, rendering resumes.
- **F_T4_3**: **Playlist Video Rotation**: Set rotation interval, play video, wait for transition to next video in playlist.
- **F_T4_4**: **Clear Playlist**: Right-click clear playlist -> rendering stops, returns to static desktop.
- **F_T4_5**: **Corruption/Inaccessible Video Path**: Invalid video path set in `config.ini` -> app handles it and falls back safely to default.
