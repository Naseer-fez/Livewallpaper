# Scope: Implementation Track

## Architecture
- The application is a Windows Live Wallpaper Engine written in C++ and Rust.
- C++ side: Main application, Window management, WorkerW injection, D3D11 rendering, Media Foundation video decoding.
- Rust side: Renders custom shaders, bridged via `ffi_shader_bridge`.
- Main entry point: `src/main.cpp`.
- Key files:
  - `src/explorer_integration.cpp` / `h` (WorkerW logic)
  - `src/device_manager.cpp` / `h` (D3D11 device creation)
  - `src/swap_chain_manager.cpp` / `h` (Swap chain Present, back buffer setup)
  - `src/video_decoder.cpp` / `h` (Media Foundation decoding)
  - `src/video_renderer.cpp` / `h` (Rendering frames onto textures)

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | Render Pipeline Diagnostic Instrumentation | Implement requirement R1: Comprehensive pipeline stage logging. | None | PLANNED |
| M2 | WorkerW / Desktop Attachment Robustness | Implement requirement R2: Robustness to different shell states and Windows builds. | M1 | PLANNED |
| M3 | D3D11 Device Creation Hardening | Implement requirement R3: Hardware/WARP fallbacks, checks, feature level logs. | M2 | PLANNED |
| M4 | Media Foundation / Video Decoder Resilience | Implement requirement R4: Source reader fallback logs, lock verification, frame counts. | M3 | PLANNED |
| M5 | Machine Environment Diagnostic Tool | Implement requirement R5: `--diagnose` command-line flag producing a report. | M4 | PLANNED |

## Interface Contracts
- The diagnostics (--diagnose flag) should write to standard output and to `%APPDATA%\LiveWallpaper\diagnostic_report.txt`.
- Existing functions and classes like `ExplorerIntegration`, `DeviceManager`, `SwapChainManager`, `VideoDecoder`, `VideoRenderer` should retain their original signatures or extend them cleanly.
