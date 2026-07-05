# E2E Test Infra: Live Wallpaper Engine

## Test Philosophy
- Opaque-box, requirement-driven. No direct linking to internal C++ modules.
- Verification mechanism: Python sub-process launching, config.ini manipulation, log files parsing, and diagnostic report verification.

## Feature Inventory
| # | Feature | Source (requirement) | Tier 1 | Tier 2 | Tier 3 | Tier 4 |
|---|---------|---------------------|:------:|:------:|:------:|:------:|
| 1 | R1: Pipeline Logging | ORIGINAL_REQUEST §R1 | 5 | 5 | ✓ | ✓ |
| 2 | R2: WorkerW Robustness | ORIGINAL_REQUEST §R2 | 5 | 5 | ✓ | ✓ |
| 3 | R3: D3D11 Hardening | ORIGINAL_REQUEST §R3 | 5 | 5 | ✓ | ✓ |
| 4 | R4: MF / Decoder Resilience | ORIGINAL_REQUEST §R4 | 5 | 5 | ✓ | ✓ |
| 5 | R5: Diagnostic CLI Tool | ORIGINAL_REQUEST §R5 | 5 | 5 | ✓ | ✓ |

## Test Architecture
- Test runner: Python script (`d:\CODE\Utlities\LiveWallpaper\tests\e2e_tests.py`), invoked via `python tests/e2e_tests.py`.
- Test case format: standard Python `unittest` class with assertions on output files, process status, and log messages.

## Real-World Application Scenarios (Tier 4)
- Scenario 1: Clean environment run (missing config.ini, fallback video checking).
- Scenario 2: Active playback and log parsing (logs show decoding of video, swap chain present, and frame flow).
- Scenario 3: Command-line diagnostic run `--diagnose` generating complete text report.
- Scenario 4: Command-line diagnostic run with invalid argument validation.
- Scenario 5: Config migration and validation checking (path traversal, wrong extension).

## Coverage Thresholds
- Tier 1: 25 test cases (5 per feature)
- Tier 2: 25 test cases (5 per feature)
- Tier 3: 5 cross-feature tests
- Tier 4: 5 scenario-level tests
- **Total: 60 test cases**
