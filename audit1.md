# LiveWallpaper Codebase Audit: Agent 1 (Graphics, Media Pipeline & Rust Interop)

## Phase 1: Executive Audit Plan

### 1. Architectural Flow Model
The rendering and presentation subsystem operates across three execution contexts and supports two distinct rendering pipelines: **Hardware/Software Video Playback** and **Interactive Rust HLSL Shaders**.

```
+---------------------------------------------------------------------------------------------------+
| [Pipeline A: Windows Media Foundation Video Playback]                                             |
|                                                                                                   |
|  IMFSourceReader (Windows Media Foundation)                                                       |
|       |                                                                                           |
|       v  (ReadSample async poll loop)                                                             |
|  DecodingThreadProc [Thread: m_decodeThread, COINIT_MULTITHREADED]                                |
|       |                                                                                           |
|       v  (Push ComPtr<IMFSample>)                                                                 |
|  SPSCRingBuffer<ComPtr<IMFSample>, 16> [m_sampleQueue]                                            |
|       |                                                                                           |
|       v  (Peek / Pop synchronized against presentation clock)                                     |
|  VideoDecoder::UpdateFrame [Thread: RenderThread, COINIT_MULTITHREADED]                           |
|       |                                                                                           |
|       +---> Hardware DXGI Path: ID3D11DeviceContext::CopySubresourceRegion                        |
|       |     (pMFTexture [NV12 Subresource] -> m_pVideoTexture [NV12 Subresource 0])               |
|       +---> Software 2D Path: IMF2DBuffer::Lock2D -> ID3D11DeviceContext::UpdateSubresource       |
|       +---> Software Contiguous Path: IMFMediaBuffer::Lock -> UpdateSubresource                   |
|       |                                                                                           |
|       v                                                                                           |
|  VideoRenderer::RenderVideoFrame [Thread: RenderThread]                                           |
|       | - Maps AspectRatioCB (Letterbox vs. Fill UV scaling matrix)                               |
|       | - Binds NV12 ShaderResourceViews (t0: R8 Y plane, t1: R8G8 UV plane)                      |
|       | - Draws Fullscreen Triangle (SV_VertexID) with BT.709 YUV->RGB Pixel Shader               |
|       v                                                                                           |
|  SwapChainManager::Present [DXGI_SWAP_EFFECT_FLIP_DISCARD]                                        |
+---------------------------------------------------------------------------------------------------+

+---------------------------------------------------------------------------------------------------+
| [Pipeline B: Interactive Rust HLSL Shaders]                                                       |
|                                                                                                   |
|  RenderThreadController -> FFIShaderBridge -> live_wallpaper_rust.dll                             |
|       |                                                                                           |
|       +-- init_shader_host:                                                                       |
|       |     Spawns notify::RecommendedWatcher background thread for file hot-reloading            |
|       |     Compiles initial HLSL with D3DCompile (vs_4_0, ps_4_0)                                |
|       |     Allocates dynamic constant buffer (ShaderUniforms, 64 bytes)                          |
|       |                                                                                           |
|       +-- render_shader_frame (Per Frame):                                                        |
|       |     Checks atomic reload flag (replaces ID3D11PixelShader if dirty)                       |
|       |     Maps constant buffer (i_time, i_resolution, i_mouse, i_audio, i_frame)                |
|       |     Binds RTV, Viewport, VS, PS, CBuffer, PrimitiveTopology                               |
|       |     Draws Fullscreen Triangle (SV_VertexID)                                               |
|       v                                                                                           |
|  SwapChainManager::Present [DXGI_SWAP_EFFECT_FLIP_DISCARD]                                        |
+---------------------------------------------------------------------------------------------------+
```

---

### 2. Explicit API Contract Assumptions & Boundary Invariants
- **Direct3D 11 Free-Threaded Device vs. Single-Threaded Context**: Under the D3D11 API specification, `ID3D11Device` is thread-safe, but `ID3D11DeviceContext` immediate contexts are strictly single-threaded. Because the background decoding thread and the render thread both interact with D3D11 resources, thread safety relies on enabling `ID3D10Multithread::SetMultithreadProtected(TRUE)` on the device/context.
- **DXGI Child Window (`WS_CHILD`) Invariants**: When attaching to `WorkerW` or `Progman`, `m_hWnd` is a child window. Under the DXGI specification for `IDXGIFactory2::CreateSwapChainForHwnd`, `pFullscreenDesc` **must be `NULL`** when `hWnd` is a child window (`WS_CHILD`). Passing a non-null fullscreen descriptor triggers `DXGI_ERROR_INVALID_CALL` (`0x887A0001`).
- **Single-Producer Single-Consumer (SPSC) Invariants**: In `SPSCRingBuffer`, the producer exclusively advances `m_writeIndex` via `Push()`, and the consumer exclusively advances `m_readIndex` via `Pop()`, `Peek()`, and `PopAndDiscard()`. Invoking `Clear()` from the producer thread violates this invariant, causing concurrent mutations of `m_readIndex` and read-after-free bugs.
- **Secure DLL Loading Contract**: Using `LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_APPLICATION_DIR` restricts the loader strictly to the application executable directory. To resolve Windows system DLLs (`d3d11.dll`, `dxgi.dll`, `d3dcompiler_47.dll`, `user32.dll`), `LOAD_LIBRARY_SEARCH_SYSTEM32` is mandatory.
- **Rust FFI Boundary Safety**: Memory across the C++/Rust boundary is owned by Rust's `Box<ShaderHost>`. C++ holds an opaque pointer (`*mut c_void`). All exported functions must prevent Rust panics from unwinding across the `extern "C"` boundary via `catch_unwind`.

---

### 3. Targeted Inspection Matrix
| Subsystem / File | Focus Area | Verifiable Check (`[Scenario] -> verify: [check]`) |
| :--- | :--- | :--- |
| `src/ffi_shader_bridge.cpp` | Dynamic Library Search Flags | `[Load DLL] -> verify: [LoadLibraryExW searches SYSTEM32 for d3d11.dll dependencies, does not return ERROR_MOD_NOT_FOUND (126)]` |
| `src/swap_chain_manager.cpp` | Child HWND SwapChain Creation | `[Attach to WorkerW] -> verify: [pFullscreenDesc is NULL for WS_CHILD window, CreateSwapChainForHwnd returns S_OK]` |
| `src/swap_chain_manager.cpp` | Resize State Synchronization | `[Failed ResizeBuffers] -> verify: [m_width/m_height not updated until RTV recreation succeeds]` |
| `src/video_decoder.cpp` | Lock-Free SPSC Producer Lifecycle | `[Video Looping] -> verify: [DecodingThreadProc does not call Clear(); consumer drains queue without race/crash]` |
| `src/video_decoder.cpp` | Timeline Loop Sample Retention | `[Loop Boundary] -> verify: [Last video frame is presented before first looped sample is accepted]` |
| `src/spsc_ring_buffer.h` | Size Calculation Wrap-Around | `[Concurrent Size()] -> verify: [Unsigned subtraction modulo 2^N with boundary check prevents SIZE_MAX underflow]` |
| `src/video_renderer.cpp` / `lib.rs` | D3D11 Pipeline Invalidation | `[Window Resize / ClearState] -> verify: [Shaders, CBs, and topology bound unconditionally every frame]` |
| `shaders/cyberpunk_synthwave_interactive.hlsl` | Raymarching Orientation | `[Shader Render] -> verify: [Ground grid rendered on bottom half of screen, sun rendered in sky on top half]` |
| `shaders/cosmic_nebula_interactive.hlsl` | Vector Normalization Div-by-Zero | `[Mouse at Offset] -> verify: [normalize(0, 0) guarded against NaN and divide-by-zero]` |
| `live_wallpaper_rust/src/lib.rs` | Initial Shader Compilation Diagnostic | `[Syntax Error in HLSL] -> verify: [InitShaderHost returns error and writes diagnostic to out_error_buffer]` |

---

## Phase 2: Defect Catalog

### Critical Defects

---

#### [BUG-FFI-02] `LoadLibraryExW` Restricts Search to App Directory, Blocking System DLL Dependencies with Error 126
- **Target File & Lines**: `src/ffi_shader_bridge.cpp:38`
- **Root Cause & Mechanism**:
  In `FFIShaderBridge::Load`:
  ```cpp
  m_rustDll = LoadLibraryExW(dllPath.c_str(), NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
  ```
  `LOAD_LIBRARY_SEARCH_APPLICATION_DIR` restricts the dependency search path *strictly* to the directory containing the application executable.
  `live_wallpaper_rust.dll` links against Windows system DLLs located in `C:\Windows\System32` (`d3d11.dll`, `dxgi.dll`, `d3dcompiler_47.dll`, `user32.dll`, `vcruntime140.dll`). Because `LOAD_LIBRARY_SEARCH_SYSTEM32` was omitted, the Windows PE loader refuses to locate these dependencies in `System32` and fails immediately with `ERROR_MOD_NOT_FOUND` (`126` / `0x0000007E`).
  *Empirical Verification*: Testing against the compiled `build/live_wallpaper_rust.dll` with `LOAD_LIBRARY_SEARCH_APPLICATION_DIR` (`0x00000200`) causes `LoadLibraryExW` to fail with `GetLastError() = 126`. Adding `LOAD_LIBRARY_SEARCH_SYSTEM32` (`0x00000800`) or using `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS` (`0x00001000`) succeeds immediately (`HMODULE = 0x3950000`).
- **Failure Scenario**: Startup failure whenever any interactive HLSL shader is selected on clean machines.
- **Surgical Remediation**:
  Combine `LOAD_LIBRARY_SEARCH_APPLICATION_DIR` with `LOAD_LIBRARY_SEARCH_SYSTEM32`:
  ```diff
  --- a/src/ffi_shader_bridge.cpp
  +++ b/src/ffi_shader_bridge.cpp
  @@ -37,3 +37,3 @@ bool FFIShaderBridge::Load() {
               // Exclusively load from the application directory, restricting dependency searches there too
  -            m_rustDll = LoadLibraryExW(dllPath.c_str(), NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
  +            m_rustDll = LoadLibraryExW(dllPath.c_str(), NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
           }
  ```

---

#### [BUG-CONC-01] Concurrent `Clear()` on SPSC Ring Buffer from Producer Thread Induces Data Race and Use-After-Free
- **Target File & Lines**: `src/video_decoder.cpp:391` and `src/spsc_ring_buffer.h:111-120`
- **Root Cause & Mechanism**:
  `SPSCRingBuffer` is a lock-free Single-Producer Single-Consumer queue: the producer exclusively advances `m_writeIndex` via `Push()`, and the consumer exclusively advances `m_readIndex` via `Pop()`, `Peek()`, and `PopAndDiscard()`.
  In `src/video_decoder.cpp:391`:
  ```cpp
  bool isEos = (hr == MF_E_END_OF_STREAM) || (flags & MF_SOURCE_READERF_ENDOFSTREAM);
  if (isEos) {
      LOG_INFO("Reached end of video stream. Looping...");
      m_pSourceReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
      m_sampleQueue.Clear(); // <-- PRODUCER INVOKES CLEAR()
  ```
  `Clear()` calls `Pop()` in a loop, mutating `m_readIndex` on the decoding thread. Concurrently, the render thread in `VideoDecoder::UpdateFrame` calls:
  ```cpp
  IMFSample* frontSample = m_sampleQueue.Peek();
  ...
  frontSample->GetSampleTime(&hnsTimestamp);
  ```
  `Peek()` returns a raw pointer (`m_buffer[offset].Get()`) without incrementing the refcount. When `Clear()` concurrently pops and resets `m_buffer[offset]`, the `IMFSample` COM object is destroyed. The render thread immediately crashes with an Access Violation (`0xC0000005`) on `frontSample->GetSampleTime`. Furthermore, clearing the queue discards the final buffered frames before presentation.
- **Failure Scenario**: Occurs on video looping transitions during video playback.
- **Surgical Remediation**:
  Remove `m_sampleQueue.Clear()` from `DecodingThreadProc`. The render thread drains the queue and resets the timeline naturally when it observes a smaller timestamp.
  ```diff
  --- a/src/video_decoder.cpp
  +++ b/src/video_decoder.cpp
  @@ -388,7 +388,6 @@ void VideoDecoder::DecodingThreadProc() {
               LOG_INFO("Reached end of video stream. Looping...");
               
               // Flush decoder pipeline to release DXVA2 buffers and prevent VRAM accumulation
               m_pSourceReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
  -            m_sampleQueue.Clear();
   
               PROPVARIANT var;
  ```

---

#### [BUG-D3D-01] Permanent Render Black Screen Due to Pipeline State Desynchronization Across Resizing and Mode Switching
- **Target File & Lines**: `src/video_renderer.cpp:244-252`, `live_wallpaper_rust/src/lib.rs:316-327`, and `src/swap_chain_manager.cpp:167-171`
- **Root Cause & Mechanism**:
  Both `VideoRenderer` and `ShaderHost` cache pipeline state using boolean flags (`m_pipelineBound` in C++ and `self.pipeline_bound` in Rust).
  During window or display resolution resize, `SwapChainManager::Resize` executes:
  ```cpp
  if (context) {
      context->OMSetRenderTargets(0, nullptr, nullptr);
      context->ClearState();
      context->Flush();
  }
  ```
  `context->ClearState()` clears all shaders, topology, and constant buffers from the D3D11 device context.
  Because `m_pipelineBound` and `self.pipeline_bound` remain `true`, neither renderer rebinds the vertex shader, pixel shader, primitive topology, or constant buffers on subsequent frames.
  Subsequent `Draw(3, 0)` calls fail silently or get dropped (`DEVICE_DRAW_VERTEX_SHADER_NOT_SET`, `DEVICE_DRAW_PRIMITIVE_TOPOLOGY_UNDEFINED`), causing a permanent black screen. Furthermore, `InvalidatePipelineState()` in `video_renderer.h` is dead code (never called anywhere), causing state corruption when alternating between shaders and video.
- **Failure Scenario**: Desktop resolution change, monitor DPI change, window resize, or playlist track switching.
- **Surgical Remediation**:
  Immediate context state binding in Direct3D 11 is lightweight (<50 ns). Unconditionally bind shaders, topology, constant buffers, and samplers every frame in both `RenderVideoFrame` and `ShaderHost::render`.
  
  In `src/video_renderer.cpp`:
  ```diff
  --- a/src/video_renderer.cpp
  +++ b/src/video_renderer.cpp
  @@ -244,9 +244,7 @@ HRESULT VideoRenderer::RenderVideoFrame(
  -    if (!m_pipelineBound) {
  -        d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
  -        d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
  -        d3dContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
  -        d3dContext->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
  -        d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  -        d3dContext->IASetInputLayout(nullptr);
  -        m_pipelineBound = true;
  -    }
  +    d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
  +    d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
  +    d3dContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
  +    d3dContext->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
  +    d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  +    d3dContext->IASetInputLayout(nullptr);
  ```

  In `live_wallpaper_rust/src/lib.rs`:
  ```diff
  --- a/live_wallpaper_rust/src/lib.rs
  +++ b/live_wallpaper_rust/src/lib.rs
  @@ -316,12 +316,9 @@ impl ShaderHost {
  -            if !self.pipeline_bound {
  -                self.context.VSSetShader(&self.vertex_shader, None);
  -                if let Some(ps) = &self.pixel_shader {
  -                    self.context.PSSetShader(ps, None);
  -                }
  -                self.context.VSSetConstantBuffers(0, Some(&[Some(self.constant_buffer.clone())]));
  -                self.context.PSSetConstantBuffers(0, Some(&[Some(self.constant_buffer.clone())]));
  -                self.context.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  -                self.context.IASetInputLayout(None);
  -                self.pipeline_bound = true;
  -            }
  +            self.context.VSSetShader(&self.vertex_shader, None);
  +            if let Some(ps) = &self.pixel_shader {
  +                self.context.PSSetShader(ps, None);
              }
  +            self.context.VSSetConstantBuffers(0, Some(&[Some(self.constant_buffer.clone())]));
  +            self.context.PSSetConstantBuffers(0, Some(&[Some(self.constant_buffer.clone())]));
  +            self.context.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  +            self.context.IASetInputLayout(None);
  ```

---

### High Severity Defects

---

#### [BUG-D3D-05] `CreateSwapChainForHwnd` Passes Fullscreen Descriptor on Child HWND (`WS_CHILD`), Violating DXGI Specification
- **Target File & Lines**: `src/swap_chain_manager.cpp:83-91,100-108`
- **Root Cause & Mechanism**:
  In `SwapChainManager::CreateSwapChain`:
  ```cpp
  DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsd = { 0 };
  fsd.RefreshRate.Numerator = 60;
  fsd.RefreshRate.Denominator = 1;
  fsd.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
  fsd.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
  fsd.Windowed = TRUE;

  hr = dxgiFactory->CreateSwapChainForHwnd(
      device,
      m_hWnd,
      &scd,
      &fsd, // <--- INVALID CALL FOR CHILD WINDOW
      NULL,
      &m_swapChain
  );
  ```
  `m_hWnd` is a child window (`WS_CHILD`) attached to `WorkerW` / `Progman`.
  Microsoft's DXGI documentation for `CreateSwapChainForHwnd` states:
  > *"For a windowed swap chain, you must set this parameter to NULL. If you specify a child window (a window that has the WS_CHILD style) for hWnd, you must set pFullscreenDesc to NULL."*
  Passing `&fsd` for a child window violates the DXGI specification and triggers `DXGI_ERROR_INVALID_CALL` (`0x887A0001`). The fallback path at line 100 repeats the same mistake with `&fsd`, causing both attempts to fail.
- **Failure Scenario**: Occurs when attaching to Windows Explorer (`WorkerW`) on strict DXGI runtimes or debug validation layers.
- **Surgical Remediation**:
  Pass `NULL` for `pFullscreenDesc` in both primary and fallback calls:
  ```diff
  --- a/src/swap_chain_manager.cpp
  +++ b/src/swap_chain_manager.cpp
  @@ -87,3 +87,3 @@ bool SwapChainManager::CreateSwapChain(ID3D11Device* device) {
           &scd,
  -        &fsd,
  +        NULL,
           NULL,
  @@ -104,3 +104,3 @@ bool SwapChainManager::CreateSwapChain(ID3D11Device* device) {
               &scd,
  -            &fsd,
  +            NULL,
               NULL,
  ```

---

#### [BUG-D3D-02] Swap Chain Size Desynchronization and Permanent Render Target Invalidation on Failed Resize
- **Target File & Lines**: `src/swap_chain_manager.cpp:157-185`
- **Root Cause & Mechanism**:
  In `SwapChainManager::Resize`:
  ```cpp
  m_width = width;
  m_height = height;
  ...
  m_renderTargetView.Reset();

  HRESULT hr = m_swapChain->ResizeBuffers(
      m_bufferCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0
  );
  if (FAILED(hr)) {
      LOG_ERROR("SwapChainManager: ResizeBuffers failed. HRESULT = 0x%08X", hr);
      return false;
  }
  return CreateRenderTargetView(device);
  ```
  `m_width` and `m_height` are mutated *before* `ResizeBuffers` and `CreateRenderTargetView` succeed. If `ResizeBuffers` fails (e.g. during display driver reset, mode switch, or VRAM pressure):
  1. `m_renderTargetView` is reset to `nullptr`.
  2. `m_width` and `m_height` retain the new dimensions, but the swapchain backbuffer is unallocated.
  3. On the next frame, line 155 executes:
     `if (width == m_width && height == m_height) return true;`
     Because `m_width` was already overwritten, it early-returns `true` without ever recreating the RTV. The engine remains stuck with a null RTV.
- **Failure Scenario**: Transient failure during `ResizeBuffers` when moving the window between monitors of differing resolutions.
- **Surgical Remediation**:
  Update `m_width` and `m_height` only after `ResizeBuffers` and `CreateRenderTargetView` succeed:
  ```diff
  --- a/src/swap_chain_manager.cpp
  +++ b/src/swap_chain_manager.cpp
  @@ -156,4 +156,2 @@ bool SwapChainManager::Resize(ID3D11Device* device, ID3D11DeviceContext* context
       if (width == m_width && height == m_height) return true;
  -    m_width = width;
  -    m_height = height;
  @@ -183,3 +181,7 @@ bool SwapChainManager::Resize(ID3D11Device* device, ID3D11DeviceContext* context
   
  -    return CreateRenderTargetView(device);
  +    if (!CreateRenderTargetView(device)) {
  +        return false;
  +    }
  +    m_width = width;
  +    m_height = height;
  +    return true;
  ```

---

#### [BUG-VID-01] Video Looping Timeline Reset Drops Last Video Sample in `UpdateFrame`
- **Target File & Lines**: `src/video_decoder.cpp:478-488`
- **Root Cause & Mechanism**:
  In `VideoDecoder::UpdateFrame`, samples are processed in a `while (true)` loop:
  ```cpp
  if (sampleTimeMs < m_currentFrameTimestamp) {
      LOG_INFO("UpdateFrame: Video loop detected. Resetting playback timeline. new sampleTimeMs = %.2f ms, previous = %.2f ms", sampleTimeMs, m_currentFrameTimestamp);
      m_playbackTimeMs = sampleTimeMs;
      m_currentFrameTimestamp = sampleTimeMs;
      Microsoft::WRL::ComPtr<IMFSample> poppedSample;
      if (m_sampleQueue.Pop(poppedSample)) {
          pSelectedSample = std::move(poppedSample);
          hasNewFrame = true;
      }
      continue;
  }
  ```
  Suppose the queue holds the final frame of the video (timestamp 9966 ms) followed by the first frame of the looped stream (timestamp 0 ms).
  - Iteration 1: The 9966 ms frame is popped and stored in `pSelectedSample`. `m_currentFrameTimestamp` updates to 9966 ms. `hasNewFrame = true`.
  - Iteration 2 (same call): `frontSample` is frame 0 (0 ms). `sampleTimeMs < m_currentFrameTimestamp` evaluates to true.
  Frame 0 is popped and moved into `pSelectedSample`, **silently overwriting and discarding the 9966 ms frame** without it ever being uploaded to the GPU or presented.
- **Failure Scenario**: Occurs on every loop boundary during continuous video playback.
- **Surgical Remediation**:
  If `pSelectedSample` has already acquired a valid frame during the current call, break out to present the final frame before consuming the loop sample:
  ```diff
  --- a/src/video_decoder.cpp
  +++ b/src/video_decoder.cpp
  @@ -477,6 +477,9 @@ bool VideoDecoder::UpdateFrame(ID3D11DeviceContext* pContext, double& outWaitTim
           }
   
           if (sampleTimeMs < m_currentFrameTimestamp) {
  +            if (hasNewFrame && pSelectedSample) {
  +                break; // Present the final frame before consuming the looped sample
  +            }
               LOG_INFO("UpdateFrame: Video loop detected. Resetting playback timeline. new sampleTimeMs = %.2f ms, previous = %.2f ms", sampleTimeMs, m_currentFrameTimestamp);
               m_playbackTimeMs = sampleTimeMs;
  ```

---

#### [BUG-SHAD-01] Inverted Y-Axis Coordinate System in Raymarched Synthwave Shader
- **Target File & Lines**: `shaders/cyberpunk_synthwave_interactive.hlsl:16,24,48` and `live_wallpaper_rust/src/lib.rs:56-69`
- **Root Cause & Mechanism**:
  In `live_wallpaper_rust/src/lib.rs`, `DEFAULT_VS_CODE` maps `TexCoord = (0, 0)` at clip space `(-1, +1)` (Top-Left of screen) and `(1, 1)` at `(+1, -1)` (Bottom-Right of screen).
  In `cyberpunk_synthwave_interactive.hlsl:16`:
  `uv = (input.UV - 0.5);`
  Top of screen: `uv.y = -0.5`. Bottom of screen: `uv.y = +0.5`.
  In lines 24 & 48:
  ```hlsl
  float3 rayDir = normalize(float3(uv.x - mouseOffset.x * 0.3, uv.y - mouseOffset.y * 0.2, 0.8));
  ...
  if (rayDir.y < 0.0) { // Raycast to plane Y = -0.4
  ```
  Because `uv.y < 0.0` corresponds to the **top** of the screen, the 3D ground plane is rendered onto the **sky** at the top of the monitor. Meanwhile, the sun (`sunUV = uv - float2(mouseOffset.x * 0.1, 0.15)`) renders at the **bottom** of the monitor. The entire synthwave scene renders upside-down.
- **Failure Scenario**: Visual inversion upon launching the Cyberpunk Synthwave interactive shader.
- **Surgical Remediation**:
  Invert the Y component of normalized UV and mouse coordinates:
  ```diff
  --- a/shaders/cyberpunk_synthwave_interactive.hlsl
  +++ b/shaders/cyberpunk_synthwave_interactive.hlsl
  @@ -15,7 +15,7 @@ struct VS_OUTPUT {
   float4 main(VS_OUTPUT input) : SV_TARGET {
  -    float2 uv = (input.UV - 0.5);
  +    float2 uv = float2(input.UV.x - 0.5, -(input.UV.y - 0.5));
       uv.x *= i_resolution.z; // Aspect ratio correction
   
       // Mouse interactive offset (-0.5 to 0.5)
  -    float2 mouseOffset = (i_mouse.xy / i_resolution.xy) - 0.5;
  +    float2 mouseOffset = float2(i_mouse.x / i_resolution.x - 0.5, -(i_mouse.y / i_resolution.y - 0.5));
       bool isClick = i_mouse.z > 0.5;
  ```

---

#### [BUG-SHAD-04] Initial Shader Syntax Errors Silently Masked by Fallback Shader
- **Target File & Lines**: `live_wallpaper_rust/src/lib.rs:126-128,461-473`
- **Root Cause & Mechanism**:
  In `ShaderHost::new`:
  ```rust
  let ps_blob = compile_shader(&user_code, "main", "ps_4_0")
      .or_else(|_| compile_shader(&user_code, "PSMain", "ps_4_0"))
      .or_else(|_| compile_shader(FALLBACK_PS_CODE, "main", "ps_4_0"))?;
  ```
  If a user makes a syntax error in their custom HLSL shader, `compile_shader(&user_code, ...)` fails with an error blob containing the compiler diagnostic.
  However, the `.or_else(...)` chain immediately falls back to `FALLBACK_PS_CODE`. Because the fallback succeeds, `ShaderHost::new` returns `Ok(host)`.
  In `init_shader_host`, it returns `S_OK`, leaving `out_error_buffer` empty. The application renders a blank blue screen with zero diagnostics in the UI or log file.
- **Failure Scenario**: Developing or editing any custom interactive HLSL shader with syntax errors.
- **Surgical Remediation**:
  Propagate the error from `compile_shader` so `out_error_buffer` receives the diagnostic:
  ```diff
  --- a/live_wallpaper_rust/src/lib.rs
  +++ b/live_wallpaper_rust/src/lib.rs
  @@ -125,4 +125,3 @@ impl ShaderHost {
           let ps_blob = compile_shader(&user_code, "main", "ps_4_0")
  -            .or_else(|_| compile_shader(&user_code, "PSMain", "ps_4_0"))
  -            .or_else(|_| compile_shader(FALLBACK_PS_CODE, "main", "ps_4_0"))?;
  +            .or_else(|_| compile_shader(&user_code, "PSMain", "ps_4_0"))?;
  ```

---

### Medium Severity Defects

---

#### [BUG-TIM-01] SPSC Ring Buffer `Size()` Out-of-Order Underflow Glitch and Wrap-Around Fix
- **Target File & Lines**: `src/spsc_ring_buffer.h:100-104`
- **Root Cause & Mechanism**:
  ```cpp
  size_t Size() const {
      const size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
      const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
      return (writeIdx >= readIdx) ? (writeIdx - readIdx) : 0;
  }
  ```
  If simplified naively to `return (writeIdx - readIdx);`, an out-of-order observation where the reader thread advances `readIdx` right between the two relaxed loads causes `writeIdx < readIdx` (e.g. `writeIdx = 5`, `readIdx = 6`). In unsigned arithmetic, `5 - 6` underflows to `SIZE_MAX` ($18,446,744,073,709,551,615$).
  In `video_decoder.cpp:360`:
  `if (m_sampleQueue.Size() < 5) break;`
  Evaluating `SIZE_MAX < 5` returns `false`, causing the decoding thread to enter an **infinite sleep deadlock**.
  Conversely, keeping `(writeIdx >= readIdx)` fails when `writeIdx` overflows `size_t` back to 0.
- **Surgical Remediation**:
  Compute unsigned difference modulo $2^N$ and guard against the transient out-of-order window:
  ```diff
  --- a/src/spsc_ring_buffer.h
  +++ b/src/spsc_ring_buffer.h
  @@ -101,4 +101,4 @@ public:
           const size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
           const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
  -        return (writeIdx >= readIdx) ? (writeIdx - readIdx) : 0;
  +        const size_t diff = writeIdx - readIdx;
  +        return (diff <= Capacity) ? diff : 0;
       }
  ```

---

#### [BUG-FFI-01] Calling Convention Mismatch on 32-bit Targets (`WINAPI` vs `extern "C"`)
- **Target File & Lines**: `src/ffi_shader_bridge.h:22-24` and `live_wallpaper_rust/src/lib.rs:419,492,548`
- **Root Cause & Mechanism**:
  In `ffi_shader_bridge.h`, function pointers use `WINAPI`:
  `typedef HRESULT(WINAPI *InitShaderHostFn)(...);`
  On x86 (32-bit), `WINAPI` expands to `__stdcall` (callee cleans stack).
  In `lib.rs`, exports are declared `pub unsafe extern "C" fn`, which maps to `__cdecl` (caller cleans stack) on 32-bit x86.
  Calling `__cdecl` exports through `__stdcall` function pointers corrupts the ESP stack pointer upon return.
- **Failure Scenario**: 32-bit Windows builds.
- **Surgical Remediation**:
  Use `extern "system"` in `lib.rs` (which expands to `stdcall` on Windows x86 and `extern "C"` on Windows x64):
  ```diff
  --- a/live_wallpaper_rust/src/lib.rs
  +++ b/live_wallpaper_rust/src/lib.rs
  @@ -419,3 +419,3 @@ unsafe fn write_error_to_buffer(err_msg: &str, out_error_buffer: *mut u16, erro
  -pub unsafe extern "C" fn init_shader_host(
  +pub unsafe extern "system" fn init_shader_host(
  @@ -492,3 +492,3 @@ pub unsafe extern "C" fn init_shader_host(
  -pub unsafe extern "C" fn render_shader_frame(
  +pub unsafe extern "system" fn render_shader_frame(
  @@ -548,3 +548,3 @@ pub unsafe extern "C" fn render_shader_frame(
  -pub unsafe extern "C" fn shutdown_shader_host(host_ptr: *mut ShaderHost) -> HRESULT {
  +pub unsafe extern "system" fn shutdown_shader_host(host_ptr: *mut ShaderHost) -> HRESULT {
  ```

---

#### [BUG-SHAD-02] HLSL Vector Normalization Division by Zero / NaN Glitch in Cosmic Nebula
- **Target File & Lines**: `shaders/cosmic_nebula_interactive.hlsl:59-62`
- **Root Cause & Mechanism**:
  In `cosmic_nebula_interactive.hlsl`:
  ```hlsl
  float distToMouse = length(uv - mouseUV);
  float distortion = 0.04 / (distToMouse + 0.08);
  float2 warpedUV = uv + normalize(uv - mouseUV + 0.0001) * distortion;
  ```
  Adding scalar `0.0001` adds to both components. When `uv - mouseUV == float2(-0.0001, -0.0001)`, the vector evaluates to `float2(0, 0)`.
  In D3D11 Shader Model 4.0, `normalize(float2(0, 0))` evaluates `v * rsqrt(dot(v, v)) = 0 * INF = NaN`.
  NaN propagates through the FBM noise octaves, resulting in black rectangular artifacts or flashing squares around the mouse cursor.
- **Failure Scenario**: Cursor positioned over exact matching coordinate points.
- **Surgical Remediation**:
  Use the precalculated `distToMouse`:
  ```diff
  --- a/shaders/cosmic_nebula_interactive.hlsl
  +++ b/shaders/cosmic_nebula_interactive.hlsl
  @@ -61,3 +61,3 @@ float4 main(VS_OUTPUT input) : SV_TARGET {
       float distortion = 0.04 / (distToMouse + 0.08);
  -    float2 warpedUV = uv + normalize(uv - mouseUV + 0.0001) * distortion;
  +    float2 warpedUV = uv + (distToMouse > 0.0001 ? ((uv - mouseUV) / distToMouse) : float2(0, 0)) * distortion;
  ```

---

#### [BUG-VID-04] Software Frame Upload Stride Misalignment in `VideoDecoder::UpdateFrame`
- **Target File & Lines**: `src/video_decoder.cpp:608-616`
- **Root Cause & Mechanism**:
  In the contiguous buffer software fallback path:
  ```cpp
  UINT32 rowPitch = m_videoWidth;
  pContext->UpdateSubresource(
      m_pVideoTexture.Get(),
      0,
      nullptr,
      pData,
      rowPitch,
      0
  );
  ```
  Media Foundation video decoders align row stride to 16, 32, or 64 bytes (e.g., a 1366x768 video is padded to stride 1376 or 1408). Hardcoding `rowPitch = m_videoWidth` causes diagonal skewing, stride tearing, and memory out-of-bounds reads on non-16-aligned resolutions.
- **Failure Scenario**: Software decoding of videos with non-16-byte-aligned widths.
- **Surgical Remediation**:
  Query the actual stride via `pCurrentType->GetUINT32(MF_MT_DEFAULT_STRIDE, &rowPitch)` or `MFGetStrideForBitmapInfoHeader`.

---

### Low Severity Defects

---

#### [BUG-VID-03] `VideoRenderer::UpdateAspectRatioCB` Lack of Zero-Dimension Bounds Checks
- **Target File & Lines**: `src/video_renderer.cpp:169-180`
- **Root Cause & Mechanism**:
  `videoAspect = (float)videoWidth / videoHeight;` and `windowAspect = (float)GetWidth() / GetHeight();`
  If `videoHeight` or `GetHeight()` is 0 (e.g. during minimized window state or early initialization), floating-point division produces `+infinity` and `NaN`, corrupting `AspectRatioCB` constants.
- **Surgical Remediation**:
  Add validation guard: `if (videoWidth <= 0 || videoHeight <= 0 || textureWidth <= 0 || textureHeight <= 0 || m_pSwapChainManager->GetWidth() <= 0 || m_pSwapChainManager->GetHeight() <= 0) return;`.

---

#### [BUG-D3D-03] Missing DXGI Device Removed Reason Diagnostic Logging
- **Target File & Lines**: `src/swap_chain_manager.cpp:187-199` and `src/render_thread_controller.cpp:365-372`
- **Root Cause & Mechanism**:
  When GPU driver crashes (TDR) occur, `Present` returns `DXGI_ERROR_DEVICE_REMOVED`. Direct3D 11 requires querying `ID3D11Device::GetDeviceRemovedReason()` to discover whether it was `DXGI_ERROR_DEVICE_HUNG`, `DEVICE_RESET`, or `DRIVER_INTERNAL_ERROR`. Currently, neither class logs this diagnostic.
- **Surgical Remediation**:
  Log `device->GetDeviceRemovedReason()` when presentation fails with `DXGI_ERROR_DEVICE_REMOVED`.

---

#### [BUG-D3D-04] Missing DXGI Window Message Association Suppression on Wallpaper Child Window
- **Target File & Lines**: `src/swap_chain_manager.cpp:83-92`
- **Root Cause & Mechanism**:
  DXGI swap chains by default hook `m_hWnd` to intercept `Alt+Enter`. For a desktop wallpaper child window attached beneath Windows Explorer desktop icons (`WorkerW` / `Progman`), DXGI shortcut hooking can interfere with desktop shell input.
- **Surgical Remediation**:
  Call `dxgiFactory->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER)` after swap chain creation.

---

## Phase 3: Cross-Subsystem Notes for Agent 2 (Shell & Lifecycle)

1. **HWND Lifecycle & Reattachment Synchronization**:
   When `ExplorerIntegration` reattaches to `WorkerW` after an explorer crash or restart, `RenderThreadController::RequestRecreate` is triggered. Agent 2 must guarantee that the old HWND is completely detached and destroyed before `TeardownMediaPipeline()` and `InitializeMediaPipeline()` run on the render thread.
2. **Context Multithreading Protection API Contract**:
   On modern Windows 10/11 D3D11 runtimes, `device->QueryInterface(__uuidof(ID3D10Multithread))` and `context->QueryInterface(__uuidof(ID3D10Multithread))` return different interface pointer addresses (`0x21a983d9488` vs `0x21a9a3556e0`), but they share the same underlying critical section. However, querying from `context` remains the standard Microsoft API contract.
3. **Occlusion Throttle vs Presentation Sync**:
   When `m_syncManager->IsThrottled()` drops presentation to 1 FPS, `SwapChainManager::Present` passes `syncInterval = 0`. With `DXGI_SWAP_EFFECT_FLIP_DISCARD`, DWM gracefully drops unpresented frames, but `Timer::PreciseSleep(1000.0)` is used in `render_thread_controller.cpp:294`. Agent 2's power/idle transitions must wake this sleep immediately upon occlusion changes.
