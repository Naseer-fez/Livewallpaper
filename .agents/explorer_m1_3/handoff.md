# Handoff Report — Explorer 3

## 1. Observation
The following file locations and API call sites in the `LiveWallpaper` project were examined:
1. **`src/main.cpp` (lines 68-74)**:
   ```cpp
   Utils::InitializeLogging();
   LOG_INFO("LiveWallpaper main application starting.");

   HRESULT hrCOM = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
   if (FAILED(hrCOM)) {
       LOG_ERROR("CoInitializeEx failed in main thread. HRESULT: 0x%08X", hrCOM);
   }
   ```
2. **`src/explorer_integration.cpp`**:
   - `Initialize` (lines 10-33): Performs serial calls to `FindWorkerW()`, `CreateHostWindow(hInstance)`, and `InjectIntoDesktop()`.
   - `FindWorkerW` (lines 50-122): Employs window enumeration and a message timeout (`SendMessageTimeoutW` with `0x052C` to Progman) to locate or spawn `WorkerW`.
   - `CreateHostWindow` (lines 124-151): Registers the window class `LiveWallpaperHostClass` and calls `CreateWindowExW`.
   - `InjectIntoDesktop` (lines 153-177): Adjusts windows parent-child relationship via `SetParent` and positions using `SetWindowPos`.
3. **`src/render_thread_controller.cpp` (lines 82-363)**:
   - `ThreadProc`: Spawns the dedicated rendering thread, initializes thread-level COM apartment (`CoInitializeEx(NULL, COINIT_MULTITHREADED)`), initializes `DeviceManager` and `SwapChainManager` (lines 106-107), initializes the video decoder (line 121), and drives the main rendering loop (lines 133-341).
4. **`src/device_manager.cpp` (lines 10-67)**:
   - `Initialize`: Calls `D3D11CreateDevice` with hardware type fallback to WARP. Acquires `ID3D10Multithread` to configure multithread safety.
5. **`src/swap_chain_manager.cpp`**:
   - `Initialize` (lines 10-31): Coordinates swap chain and RTV allocations.
   - `CreateSwapChain` (lines 42-91): Performs DXGI factory enumeration and sets up `DXGI_SWAP_CHAIN_DESC1`.
   - `CreateRenderTargetView` (lines 93-104): Allocates RTV.
   - `Resize` (lines 106-134): Handles window resizing through DXGI `ResizeBuffers`.
   - `Present` (lines 136-140): Invokes DXGI `Present` on the swap chain.
6. **`src/video_decoder.cpp`**:
   - `Initialize` (lines 12-54): Configures device multithreading, starts Media Foundation (`MFStartup`), and creates DXGI Device Manager (`MFCreateDXGIDeviceManager`/`ResetDevice`).
   - `LoadVideo` (lines 64-208): Sequentially tests four fallback combinations for Media Foundation `IMFSourceReader` initialization.
   - `UpdateFrame` (lines 387-571): Retrieves samples from the queue and copies them to the texture using either the GPU-to-GPU hardware path (`IMFDXGIBuffer` / `CopySubresourceRegion`), software 2D path (`IMF2DBuffer` / `UpdateSubresource`), or software 1D contiguous path (`IMFMediaBuffer::Lock` / `UpdateSubresource`).
7. **`src/video_renderer.cpp` (lines 206-248)**:
   - `RenderVideoFrame`: Sets up viewport details, configures constant buffer mapping, binds Y/UV shader resource views (SRVs), and issues `Draw(3, 0)`.

---

## 2. Logic Chain
To satisfy Requirement R1 (Render Pipeline Diagnostic Instrumentation), we must ensure that:
1. **Entry, Success/Failure, and Contextual Info** are logged at each key pipeline stage.
2. Comparing working vs failing logs immediately identifies the point of failure.
3. Performance is not significantly degraded by logging (especially inside hot paths like `Present` or `RenderVideoFrame`).

The logical mapping from observations to the proposed changes is as follows:
- **Application Start**: We introduce logging right before and after `CoInitializeEx` in `src/main.cpp` using `LOG_INFO`/`LOG_ERROR` to capture initialization success or failure immediately.
- **Desktop Window Discovery & Attachment**: In `src/explorer_integration.cpp`, we record every step of the WorkerW search and window attachment sequence, logging window handles (with `%p` formatting) and virtual screen metrics.
- **DX11 Graphics Device Manager**: In `src/device_manager.cpp`, we query DXGI factory interfaces prior to device creation to log details such as device name, vendor IDs, and dedicated memory. We log the successful feature level and the HRESULT results of both hardware and software (WARP) attempts.
- **Swap Chain & Buffers**: In `src/swap_chain_manager.cpp`, we log the `DXGI_SWAP_CHAIN_DESC1` parameters (dimensions, formats, buffer count). Inside `Present()`, we log every HRESULT returned. To avoid disk I/O bottlenecks in release builds, we use `LOG_DEBUG` for successful presents and `LOG_ERROR` for failures.
- **Media Foundation Decoder Fallbacks**: In `src/video_decoder.cpp`, we log each of the four fallback attempts during `LoadVideo` to show exactly which URL/attribute combinations succeeded or failed. We extract the codec type using `MF_MT_SUBTYPE` and log it.
- **Frame Extraction Path**: In `VideoDecoder::UpdateFrame`, we log the chosen decoding path (Hardware GPU-to-GPU, Software 2D, or Software 1D Contiguous) along with frame sizes.
- **Milestone Detection**: In `RenderThreadController::ThreadProc`, we implement a state variable `m_firstFrameMilestoneLogged` to detect and log the first frame milestone when `frameUpdated` is true and `Present()` returns `S_OK`.

---

## 3. Caveats
- **Log Spam on Hot Paths**: Hot paths like `SwapChainManager::Present` and `VideoRenderer::RenderVideoFrame` are executed up to 60+ times per second. Writing these logs to disk continuously will cause severe frame drops and fill storage. Therefore, we utilize the `LOG_DEBUG` macro, which is compiled out of non-debug builds, while only logging errors using `LOG_ERROR`.
- **Rust Integration**: The shader pipeline uses an opaque Rust host pointer (`m_shaderHost`) via `FFIShaderBridge`. The Rust code internals were not investigated as they are outside the scope of C++ render pipeline instrumentation.

---

## 4. Conclusion
We have identified the exact files and methods needing modification to implement Requirement R1. The proposed plan places structured logging throughout the codebase, making it trivial to diagnose rendering issues on client devices by inspecting the generated `log.txt`.

---

## 5. Verification Method
Since this is a read-only investigation, the verification steps below should be performed by the implementer:
1. **Build the project**: Run `cmake --build build` from the project root to compile the instrumented files.
2. **Run basic tests**: Execute the project tests by running the target test executable `LiveWallpaperTests` (defined in `CMakeLists.txt` via `add_test`).
3. **Run the Application**: Launch `LiveWallpaper.exe` with a test video playing.
4. **Inspect Log Output**: Open the log file at `%APPDATA%\LiveWallpaper\log.txt` and confirm:
   - All initialization logs for COM, WorkerW, D3D11 device, and swap chain are present.
   - The selected graphics adapter information and video codec are printed.
   - The "MILESTONE" log occurs exactly once when the first frame is successfully rendered and presented.
5. **Verify Hot Path Exclusions**: Build in Release configuration and confirm that no `DEBUG` logs spam the log file, avoiding performance bottlenecks.
