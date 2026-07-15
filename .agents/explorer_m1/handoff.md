# Handoff Report — explorer_m1

This handoff report summarizes the codebase analysis of the LiveWallpaper project. It identifies the root causes and proposes fix strategies for 8 specific issues.

## 1. Observation
* **PathMessage* memory leaks**:
  - `src/synchronization_manager.h` (Lines 7–10, 56–57) defines `PathMessage` and `m_pathQueue` of type `SPSCRingBuffer<PathMessage*, 16>`.
  - `src/synchronization_manager.cpp` (Lines 63–68) allocates `new PathMessage` and deletes it if `m_pathQueue.Push` fails.
  - `src/synchronization_manager.cpp` (Lines 70–80) deletes `msg` upon popping it in `PopVideoChange`.
  - `src/synchronization_manager.cpp` (Lines 82–84) calls `m_pathQueue.Clear()`.
* **IMFSample* reference leaks**:
  - `src/video_decoder.h` (Line 75) declares `SPSCRingBuffer<IMFSample*, 16> m_sampleQueue;`.
  - `src/video_decoder.cpp` (Lines 234–259) defines `CloseVideo()`, which calls `m_sampleQueue.Clear()` after joining `m_decodeThread`.
  - `src/video_decoder.cpp` (Lines 440–491) defines `UpdateFrame()`. It pops samples and attaches them to `pSelectedSample` (a `ComPtr`).
* **Destructor behavior of lock-free collections**:
  - `src/spsc_ring_buffer.h` (Lines 15–17, 89–96) contains `~SPSCRingBuffer()` calling `Clear()`, which relies on `item->Release()`.
* **Concurrency on ID3D11DeviceContext**:
  - `src/video_decoder.cpp` (Lines 423–620) uses `ID3D11DeviceContext* pContext` inside `UpdateFrame` for texture copy/updates.
  - `live_wallpaper_rust/src/lib.rs` (Lines 269–335) uses `ID3D11DeviceContext` for shader mapping and drawing.
* **Texture reallocation thread-safety**:
  - `src/video_decoder.cpp` (Lines 270–299) defines `ReallocateVideoTexture()`, resetting `m_pVideoSRV_Y`, `m_pVideoSRV_UV`, and `m_pVideoTexture` before recreating them.
* **Window destruction & recreation synchronization**:
  - `src/explorer_integration.cpp` (Lines 201–246) defines `Update()`, executing `Shutdown()` (destroying `m_hWnd`) and `Initialize()` synchronously.
  - `src/main.cpp` (Lines 360–365) checks for recreated handle and notifies the render thread via `renderThread.RequestRecreate(currentHWnd)` after `host.Update()` returns.
* **Window dimension caching**:
  - `src/main.cpp` (Lines 367–374) invokes `renderThread.RequestResize(w, h)` on every message loop iteration.
* **Shader compilation propagation & secure DLL loading**:
  - `live_wallpaper_rust/src/lib.rs` (Lines 123–140) catches compilation errors, outputs to `eprintln!`, and compiles fallback shader.
  - `src/ffi_shader_bridge.cpp` (Lines 10–29) uses `LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_APPLICATION_DIR`.

## 2. Logic Chain
1. **Manual lifetime / Multi-producer**: Pushing raw pointers (`PathMessage*`) into a lock-free queue forces manual `delete` on failure. If multiple threads call `RequestChangeVideo` concurrently, index corruption in the SPSC queue can cause memory to leak or be double-freed. Using `std::unique_ptr` avoids leaks on early returns or push failures.
2. **Buffer loop blockage / Attach leaks**: In `UpdateFrame()`, not flushing the sample queue when the video loops leaves late frames blocking the queue. When frames are popped during catch-up, any exception or failure in the loop before attaching can cause raw pointer leaks.
3. **Destructor coupling**: Hardcoding `.Release()` in `SPSCRingBuffer::Clear` limits elements to pointer types, preventing safe usage of standard smart pointers.
4. **Context boundaries**: Direct3D 11 immediate contexts are thread-hostile. While the decoder thread only uses the thread-safe device manager, the render thread must strictly serialize context usage.
5. **Reallocation corruption**: If `ReallocateVideoTexture` fails after resetting member variables, the active SRVs still point to released resources, causing crashes on subsequent redraws.
6. **Destruction race**: Destroying `m_hWnd` on the main thread while the render thread is actively calling `Present` on the swap chain causes GPU crashes. Detaching first ensures a safe teardown.
7. **Resize spam**: Spamming `RequestResize` every tick triggers `SwapChainManager::Resize` and `ResizeBuffers` on every frame, stalling the GPU pipeline and bypassing frame pacing.
8. **Swallowed compile errors**: Creating a valid Rust `ShaderHost` by falling back to the default shader on compile failures returns `S_OK` to C++, completely hiding user shader compile errors.

## 3. Caveats
* The team did not test behavior on systems with multiple graphics cards (hybrid GPU setup) which might affect device loss recovery.
* Secure DLL validation via WinVerifyTrust assumes the executable has a valid digital signature.

## 4. Conclusion
* **Memory & Reference Leaks**: Use `std::unique_ptr` for `PathMessage*` pushing, redesign `SPSCRingBuffer` to support move-only types (`std::unique_ptr` / `ComPtr`), and flush the queue upon video loop reset.
* **Concurrency & Safety**: Enforce context isolation (immediate context only on the render thread), use transactional allocation in `ReallocateVideoTexture`, and perform a teardown handshake before destroying `m_hWnd`.
* **Performance**: Cache window size in the message loop to stop the resize-stalling bug.
* **Shaders & DLL**: Return detailed compile error strings to C++ and enforce DLL hash/signature checks.

## 5. Verification Method
* Run C++ tests: `build\tests\Debug\LiveWallpaperTests.exe` (specifically the `SPSCRingBufferBasic` tests).
* Run the Python end-to-end suite: `python tests\e2e_test_runner.py` (which validates loop counts, window recreation, and CPU/VRAM usage).
* Check the debug logs (`err.txt` or `out.txt`) to ensure zero reallocations occur when window sizes match, and verify that HLSL syntax errors are printed to logs.
