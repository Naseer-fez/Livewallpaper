# Original User Request

## Initial Request — 2026-06-15T14:42:02Z

Debug a custom Windows Live Wallpaper Engine (C++/Rust/D3D11) that renders successfully on the development machine but fails to display the wallpaper on target machines (the app launches, runs, doesn't crash, but the wallpaper never appears visually).

Working directory: d:\CODE\Utlities\LiveWallpaper
Integrity mode: development

Reference: The open-source Lively Wallpaper project (https://github.com/rocksdanister/lively) can be consulted for comparison on WorkerW injection techniques, but do not copy code directly.

## Requirements

### R1. Render Pipeline Diagnostic Instrumentation

Add comprehensive diagnostic logging throughout the entire render pipeline — from application start through Media Foundation initialization, video decoding, D3D11 device/swap chain creation, frame extraction, texture upload, WorkerW discovery, desktop attachment, and the render loop's `Present()` call. Every pipeline stage must log entry, success/failure with HRESULT codes, and contextual data (window handles, dimensions, feature levels, adapter info, codec selection). The logging must be structured so that comparing logs from a working dev machine vs a failing target machine immediately reveals the exact divergence point.

Specific pipeline stages that MUST be instrumented:
- Application Start → CoInitializeEx
- ExplorerIntegration::Initialize → FindWorkerW → CreateHostWindow → InjectIntoDesktop
- RenderThreadController::ThreadProc → DeviceManager::Initialize → SwapChainManager::Initialize
- VideoDecoder::Initialize → MFStartup → DXGI Device Manager → Source Reader fallback chain
- VideoDecoder::LoadVideo → all 4 fallback combinations with per-attempt logging
- VideoDecoder::UpdateFrame → hardware vs software path selection, frame validity
- VideoRenderer::RenderVideoFrame → SRV binding, viewport, draw call
- SwapChainManager::Present → HRESULT of every Present() call
- First frame milestone: log when the very first frame is successfully decoded AND presented

### R2. WorkerW / Desktop Attachment Robustness

Investigate and fix the Explorer integration layer (`src/explorer_integration.cpp`) for robustness across different Windows versions, configurations, and shell states. The current WorkerW discovery using `SendMessageTimeout(progman, 0x052C, ...)` and subsequent window enumeration may behave differently across Windows 10/11 builds.

Specific issues to address:
- The `0x052C` message to Progman may not always spawn a WorkerW on all Windows builds
- The WorkerW enumeration logic needs to correctly identify the wallpaper WorkerW even when multiple WorkerW windows exist
- The host window created with `WS_CHILD | WS_VISIBLE` and parented to WorkerW must be verified to actually be visible (not zero-sized, not behind another window, not on the wrong monitor)
- After `SetParent` and `SetWindowPos`, verify the final window state (parent, position, size, visibility, z-order)
- Add a post-injection verification step that confirms the host window is actually receiving paint messages or is in a renderable state
- Consider adding `EnumWindows`-based enumeration as a fallback if the `FindWindowEx` chain fails
- Log the complete Explorer window hierarchy (Progman → SHELLDLL_DefView → WorkerW chain) for diagnostic comparison between machines

### R3. D3D11 Device Creation Hardening

The D3D11 device creation in `src/device_manager.cpp` uses `D3D11_CREATE_DEVICE_VIDEO_SUPPORT` flag and requests feature levels 11_1 through 10_0. On target machines with older GPUs, integrated graphics, or outdated drivers, this may silently fail or fall through to WARP in a way that breaks the NV12 texture pipeline.

Specific issues to address:
- Log the GPU adapter name, vendor ID, device ID, driver version, and dedicated video memory at device creation time
- If hardware device creation fails and WARP fallback is used, log a prominent warning and verify that NV12 textures are supported on WARP
- After device creation, explicitly check `ID3D11Device::CheckFormatSupport` for `DXGI_FORMAT_NV12` and log whether texture creation and SRV creation are supported
- The swap chain uses `DXGI_SWAP_EFFECT_FLIP_DISCARD` — if this fails on certain configurations, fall back to `DXGI_SWAP_EFFECT_DISCARD`
- Log the actual feature level that was selected (not just what was requested)
- After swap chain creation via `CreateSwapChainForHwnd`, verify the swap chain's back buffer dimensions match expectations (not 0x0)
- Check that `CreateRenderTargetView` succeeds and the RTV is non-null before proceeding to render

### R4. Media Foundation / Video Decoder Resilience

The video decoder (`src/video_decoder.cpp`) attempts 4 fallback combinations for Source Reader creation. On target machines, hardware codecs may not be available, the NV12 output format may not be supported, or DXGI Device Manager handshake may silently produce null/corrupt frames.

Specific issues to address:
- Log which of the 4 fallback combinations actually succeeded on startup
- After successful Source Reader creation, log the negotiated output media type (format, dimensions, frame rate)
- In `DecodingThreadProc`, detect and log when `ReadSample` returns a null sample (pSample == nullptr) without an end-of-stream flag — this indicates a decoder stall
- In `UpdateFrame`, when extracting frames via the IMFDXGIBuffer hardware path, verify the texture is valid (non-null, dimensions match) before calling `CopySubresourceRegion`
- In the software fallback paths (IMF2DBuffer and raw Lock), verify the buffer size is correct for the expected NV12 layout (width * height * 1.5 bytes)
- Add a frame counter that logs every N frames (e.g., every 100th frame) to confirm frames are actually flowing through the pipeline
- If no frames have been decoded after 5 seconds of the decoder running, log a critical warning

### R5. Machine Environment Diagnostic Tool

Create a diagnostic mode activated by a `--diagnose` command-line flag that, when run on any target machine, produces a comprehensive environment report without attempting to render.

The report must include:
- Windows version, build number, and edition
- All GPU adapters (name, vendor, driver version, dedicated VRAM) via DXGI enumeration
- D3D11 device creation result with each feature level attempted
- NV12 format support check via `CheckFormatSupport`
- DXGI_SWAP_EFFECT_FLIP_DISCARD support verification
- Media Foundation codec enumeration (list available H.264 and HEVC decoders, both hardware and software)
- DPI scaling (system DPI and per-monitor DPI awareness)
- Monitor count, resolutions, and refresh rates
- DWM composition state
- Complete WorkerW/Progman/SHELLDLL_DefView window hierarchy dump
- Whether the `0x052C` message to Progman successfully produces a WorkerW

The report must be saved to `%APPDATA%\LiveWallpaper\diagnostic_report.txt` and also written to stdout.

## Acceptance Criteria

### Diagnostic Logging
- [ ] Every pipeline stage (MFStartup, Source Reader creation, D3D11CreateDevice, SwapChain creation, WorkerW discovery, host window injection, first frame decode, first Present) logs entry, result, and diagnostic data
- [ ] The log output from a target machine run can be diff'd against a dev machine run to identify the exact failure point within the first 50 log lines
- [ ] GPU adapter name, driver version, and D3D feature level are logged at startup
- [ ] WorkerW handles, parent window handles, and host window dimensions are logged with hex HWND values
- [ ] A "first frame milestone" log line confirms when the first frame has been successfully decoded and presented

### WorkerW Robustness
- [ ] The application handles the case where `SendMessageTimeout(0x052C)` does not produce a WorkerW window
- [ ] The application correctly identifies the wallpaper WorkerW even when multiple WorkerW windows exist
- [ ] The host window's final parent, position, size, and visibility state are verified and logged after injection
- [ ] Recovery from Explorer restarts continues to work
- [ ] The complete window hierarchy is logged for diagnostic purposes

### D3D11 Hardening
- [ ] Device creation logs the actual adapter name and selected feature level
- [ ] If hardware device creation fails, the WARP fallback is logged with a warning and continues to function
- [ ] NV12 format support is explicitly checked via CheckFormatSupport and logged
- [ ] SwapChain creation has a fallback from FLIP_DISCARD to DISCARD if the former fails
- [ ] SwapChain back buffer dimensions are verified non-zero after creation
- [ ] RenderTargetView is verified non-null before any render calls

### Media Foundation Resilience
- [ ] The decoder logs which of the 4 fallback combinations succeeded
- [ ] When hardware decoding fails, software fallback is attempted and logged
- [ ] Empty/null frames from ReadSample are detected and logged (not silently consumed)
- [ ] The first successfully decoded frame logs its dimensions, format, and whether it came from hardware or software path
- [ ] A frame flow counter confirms frames are being produced (logged every 100 frames)
- [ ] A 5-second timeout warning fires if no frames have been decoded

### Environment Diagnostic
- [ ] Running the app with `--diagnose` produces a machine environment report without attempting rendering
- [ ] The report includes: Windows version, GPU name, driver version, D3D feature level, NV12 support, MF codec list, DPI, monitor info, DWM state, WorkerW hierarchy
- [ ] The report is saved to `%APPDATA%\LiveWallpaper\diagnostic_report.txt`
- [ ] The report is also printed to stdout

### Build Verification
- [ ] The project compiles successfully with the existing CMake + MSVC toolchain after all changes
- [ ] All existing unit tests pass
- [ ] Existing functionality (video playback, shader rendering, tray icon, playlist, pause/resume, Explorer recovery) is preserved with no regressions
- [ ] The `--diagnose` flag does not interfere with normal operation when not specified
