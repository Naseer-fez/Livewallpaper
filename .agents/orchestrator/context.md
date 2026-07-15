# Project Context: Live Wallpaper Engine Fixes

## System Environment
- **OS**: Windows
- **Graphics API**: Direct3D11 (D3D11)
- **Language**: C++17, Rust
- **Main workspace**: `d:\CODE\Utlities\LiveWallpaper`

## Active Targets & Scope
We are working on five distinct requirements identified during independent code review (Follow-up requirements):
1. **Memory & Reference Leaks**:
   - `PathMessage*` in `SynchronizationManager::RequestChangeVideo` and `RequestAddVideo` queue push failures.
   - `IMFSample*` in `VideoDecoder::UpdateFrame` and `CloseVideo` discarded/left in `SPSCRingBuffer`.
   - Raw pointers/COM reference cleanup in lock-free collection/manager destructors.
2. **D3D11 Concurrency**:
   - Thread-safe access to `ID3D11DeviceContext` between `RenderThreadController` (render thread) and `VideoDecoder`/`DecodingThreadProc` (decoder thread).
   - Thread-safe texture reallocations in the video decoder (avoid racing with SRV updates).
3. **Window/Explorer Safety**:
   - Synchronize window destruction/recreation in `ExplorerIntegration::RecoverFromExplorerRestart` with render thread controller.
   - Pause render loop before destroying `m_hWnd` to avoid Present on dead handle.
   - Exponential backoff for `WorkerW` injection in `ExplorerIntegration`.
4. **Resize Polling**:
   - Cache window dimensions in main message loop; avoid redundant `RequestResize` and bypassing frame pacing.
5. **Rust FFI & Security**:
   - Propagate HLSL compile errors from Rust to C++.
   - Secure DLL loading process (DLL path validation/resolution).
