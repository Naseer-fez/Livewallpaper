# Project: Live Wallpaper Engine Robustness

## Architecture
The application is a Windows Live Wallpaper Engine written in C++.
It consists of several modules:
- **Main App Loop (`main.cpp`)**: Application startup, configuration loading, tray icon, and watchdog loop.
- **Explorer Integration (`explorer_integration.cpp`)**: Responsible for sending messages to Progman to spawn a WorkerW window and parent the host window to it.
- **Render Thread Controller (`render_thread_controller.cpp`)**: Manages the render thread, loop execution, and orchestration between device creation, swap chain, video decoding, and rendering.
- **Device Manager (`device_manager.cpp`)**: Handles DXGI Adapter enumeration, D3D11 Device creation, and checking feature support.
- **Swap Chain Manager (`swap_chain_manager.cpp`)**: Handles DXGI swap chain creation parented to the host window, back buffer extraction, render target views, and Presenting.
- **Video Decoder (`video_decoder.cpp`)**: Utilizes Media Foundation (MFStartup, Source Reader) to load video files, decode frames, upload to DXGI/D3D11 textures, and handle fallback paths.
- **Video Renderer (`video_renderer.cpp`)**: Takes decoded NV12 frames, binds them to Shader Resource Views (SRVs), runs a custom pixel shader to convert NV12 to RGB, and draws a full-screen quad.

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | E2E Test Suite | Implement a comprehensive test suite (Tiers 1-4) in Python | None | PLANNED |
| 2 | Code Implementation (R1-R5) | Add logging, harden WorkerW attachment, harden D3D11 creation, improve Media Foundation resilience, implement CLI diagnostic tool | M1 | PLANNED |
| 3 | Review & Hardening | Peer review, Challenger checks, Forensic audit verification | M1, M2 | PLANNED |

## Interface Contracts
- **Logging Interface (`src/utils.h` or equivalent)**: Should support structured format or standard string formatting, printing both to `%APPDATA%\LiveWallpaper\log.txt` and optionally console.
- **Explorer Integration**: `ExplorerIntegration::Initialize(HWND& hostWnd)` should return a boolean success code and log details of Progman, SHELLDLL_DefView, and WorkerW handles.
- **Video Decoder ↔ Video Renderer**: VideoDecoder exposes NV12 texture or lockable buffers. VideoRenderer consumes these textures, ensuring correct dimensions and SRV creation.
- **Device Manager**: Exposes D3D11 device and immediate context.

## Code Layout
- `src/main.cpp`
- `src/explorer_integration.cpp` / `src/explorer_integration.h`
- `src/device_manager.cpp` / `src/device_manager.h`
- `src/swap_chain_manager.cpp` / `src/swap_chain_manager.h`
- `src/video_decoder.cpp` / `src/video_decoder.h`
- `src/video_renderer.cpp` / `src/video_renderer.h`
- `src/render_thread_controller.cpp` / `src/render_thread_controller.h`
- `src/utils.cpp` / `src/utils.h`
