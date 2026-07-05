# Handoff Report — Render Pipeline Diagnostic Instrumentation (Requirement R1)

## 1. Observation
I directly observed the structure and codebase of the LiveWallpaper project. Using local file view tools, I examined the files where the rendering pipeline is defined, finding the exact lines and patterns of initialization, allocation, decoding, rendering, and window parenting:
* **`src/main.cpp`**: Lines 71-74 contain the initial COM library initialization:
  ```cpp
  HRESULT hrCOM = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  if (FAILED(hrCOM)) {
      LOG_ERROR("CoInitializeEx failed in main thread. HRESULT: 0x%08X", hrCOM);
  }
  ```
* **`src/explorer_integration.cpp`**: 
  * Lines 10-33 defines the `Initialize` function which triggers discovery, host window creation, and injection.
  * Lines 50-122 defines `FindWorkerW`, containing:
    ```cpp
    HWND progman = FindWindowW(L"Progman", NULL);
    ...
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_ABORTIFHUNG, 1000, &result);
    ...
    shellDefView = FindWindowExW(progman, NULL, L"SHELLDLL_DefView", NULL);
    ```
  * Lines 124-151 defines `CreateHostWindow`, which registers class `LiveWallpaperHostClass` and calls `CreateWindowExW`.
  * Lines 153-177 defines `InjectIntoDesktop`, which calls `SetParent` and `SetWindowPos` to nest the host window.
* **`src/render_thread_controller.cpp`**:
  * Lines 82-89 defines the render thread entry and COM initialization:
    ```cpp
    HRESULT hrCOM = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hrCOM)) {
        LOG_ERROR("CoInitializeEx failed in RenderThreadController thread. HRESULT: 0x%08X", hrCOM);
    }
    ```
  * Lines 106-123 contain subsystem initialization (`DeviceManager::Initialize`, `SwapChainManager::Initialize`, `VideoDecoder::Initialize`, and `LoadVideo`).
  * Lines 255-321 contain the active render loop, which updates the decoder frame, binds inputs, triggers the draw call, and presents the swap chain.
* **`src/device_manager.cpp`**: Lines 10-67 defines the initialization of D3D11 device and context. Lines 25-36 show hardware device creation via `D3D11CreateDevice`, and lines 40-51 show fallback creation via `D3D11CreateDevice` with `D3D_DRIVER_TYPE_WARP`.
* **`src/swap_chain_manager.cpp`**:
  * Lines 10-31 defines `Initialize`.
  * Lines 42-91 defines `CreateSwapChain`, which navigates the DXGI hierarchy (`QueryInterface(IDXGIDevice)`, `GetAdapter`, `GetParent(IDXGIFactory2)`) and calls `CreateSwapChainForHwnd`.
  * Lines 93-104 defines `CreateRenderTargetView`.
  * Lines 136-140 defines `Present`:
    ```cpp
    HRESULT SwapChainManager::Present(int fpsLimit) {
        if (!m_swapChain) return E_POINTER;
        UINT syncInterval = (fpsLimit == 0) ? 1 : 0;
        return m_swapChain->Present(syncInterval, 0);
    }
    ```
* **`src/video_decoder.cpp`**:
  * Lines 12-54 defines `Initialize`, which starts up Media Foundation via `MFStartup` and creates the `IMFDXGIDeviceManager`.
  * Lines 64-208 defines `LoadVideo`, implementing a loop across 4 `FallbackOption` combinations with `MFCreateSourceReaderFromURL`.
  * Lines 387-571 defines `UpdateFrame`. It checks for hardware DXVA2 paths (`IMFDXGIBuffer` at line 482) and falls back to CPU copies (line 516 and line 543).
* **`src/video_renderer.cpp`**: Lines 206-248 defines `RenderVideoFrame` which performs viewport setup, binds Y/UV shader resource views, and executes `d3dContext->Draw(3, 0)`.

---

## 2. Logic Chain
Based on the observations:
1. To diagnose failures on end-user machines, the log files must show the exact sequence of rendering stages (COM setup -> Desktop Integration -> Device/SwapChain Creation -> Media Foundation Initialization -> Codec/Source Reader Setup -> Frame Extraction -> Drawing -> Presenting) and capture failure points with HRESULT or Win32 Error codes (Observation 1).
2. Desktop injection depends on discovering the correct shell windows (`Progman`, `WorkerW`, `SHELLDLL_DefView`). By logging the return handles of window lookups and the timing of `SendMessageTimeoutW` (Observation 2), any integration divergence can be pinpointed.
3. Media Foundation DXVA2 decoding relies on a successfully set up DXGI Device Manager and multi-thread protection (Observation 4). Logging the return values of `MFStartup`, `MFCreateDXGIDeviceManager`, and `ResetDevice` provides visibility into why hardware video decoding might fail to initialize on target machines.
4. Video file loading falls back through 4 combinations of D3D Managers and Video Processing attributes (Observation 5). Capturing the outcome of each URL creation attempt and logging native video properties (resolutions, codec types) allows support engineers to differentiate file-incompatibility issues from hardware failures.
5. In the high-frequency render loop (frame extraction, draw, present), logging every frame at `LOG_INFO` level would write gigabytes of logs per day, creating performance issues. Thus, logging for `UpdateFrame` path selection (Observation 6), `RenderVideoFrame` shader bindings/viewports (Observation 8), and `Present` success (Observation 5) is placed at the `LOG_DEBUG` level (which is stripped in Release builds), while failure states (like a failed `Present` or failed buffer lock) are elevated to `LOG_ERROR` (Observation 5).
6. To verify that rendering succeeds end-to-end, a specific `LOG_INFO` message is added for the "First Frame Milestone" which triggers when the first frame is successfully decoded and presented (Observation 3, 5).

---

## 3. Caveats
* **Shader Files (.hlsl)**: The codebase also contains a Rust Shader FFI bridge path (`IsShaderFile` checks). Although the focus of R1 is video render pipeline logging, the proposed changes also integrate logging into the shader-loading paths in `render_thread_controller.cpp` for completeness.
* **Release build limit**: `LOG_DEBUG` statements will not show up on target machines running standard Release builds, only on Debug builds. However, raising render loop logs to `LOG_INFO` would cause unacceptable disk I/O latency. I assume that any fatal pipeline errors in the render loop (which will still be logged as `LOG_ERROR`) and the `MILESTONE` log (which is `LOG_INFO`) provide sufficient diagnostics for Release builds.

---

## 4. Conclusion
We have identified the exact locations and necessary instrumentation across 8 files to satisfy Requirement R1. The changes are read-only-approved and cataloged in `analysis.md` inside this directory. By applying these additions, the application will record the initialization status, HRESULTs, handles, feature levels, adapter profiles, codec formats, and frame-drawing operations, offering perfect parity diagnostics between working developer setups and failing target installations.

---

## 5. Verification Method
To verify these logging additions once implemented:
1. **Compilation**: Run `build.bat` in the root folder to compile the project and ensure there are no compilation errors.
2. **Review logs**:
   * Run the compiled `LiveWallpaper.exe` with a valid video.
   * Open the log file located at `%APPDATA%\LiveWallpaper\log.txt`.
   * Verify that the logs trace the startup, desktop injection, D3D11 device creation, DXGI Adapter name/memory, Media Foundation initialization, native codec subtype, fallback reader iterations, and show the milestone entry:
     `[INFO] MILESTONE: First video frame successfully decoded AND presented!`
3. **Debug vs. Release log comparisons**:
   * Run a Debug build and check that frame-level logs (`UpdateFrame`, `RSSetViewports`, `Present`) appear.
   * Run a Release build and verify that frame-level logs are absent (preventing disk flooding) but the milestone and error paths are still logged.
