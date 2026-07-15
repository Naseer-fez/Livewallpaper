# Codebase Analysis Report

This report identifies the root causes and proposes concrete fix strategies for the 8 identified issues in the LiveWallpaper application.

---

## 1. Heap Memory Leaks of `PathMessage*` in `SynchronizationManager`
* **Target File**: `src/synchronization_manager.cpp` (Lines 63–80), `src/synchronization_manager.h` (Lines 7–10, 56–57)
* **Direct Observation**:
  ```cpp
  void SynchronizationManager::RequestChangeVideo(const std::wstring& path) {
      PathMessage* msg = new PathMessage{ path };
      if (!m_pathQueue.Push(msg)) {
          delete msg;
      }
  }
  ```
* **Root Cause**:
  1. **Manual Lifetime Management**: The queue `m_pathQueue` is a lock-free single-producer single-consumer (SPSC) ring buffer holding raw pointers (`PathMessage*`). If a push fails, the code deletes the message. However, if a push succeeds, ownership is transferred. If the application shuts down or clears the queue via `Clear()`, it relies on the custom `SPSCRingBuffer::Clear()` function to pop and manually call `item->Release()`, which deletes the pointer.
  2. **Multi-Producer Violation**: `SPSCRingBuffer` is strictly a Single-Producer Single-Consumer queue. `RequestChangeVideo` is called from the main thread (UI callbacks). If the application is ever modified to request video changes from multiple threads concurrently (e.g. background power monitors, timers, or hotkey threads), concurrent calls to `Push` on the SPSC queue will corrupt indices, leading to lost writes (leaking memory) or double-frees.
  3. **Hypothetical `RequestAddVideo`**: If a new function `RequestAddVideo` is implemented similarly without checking push failure and deleting the allocated message on failure, it will leak memory.
* **Fix Strategy**:
  1. **Smart Pointer Ownership**: Use `std::unique_ptr<PathMessage>` to manage the memory prior to a successful push.
     ```cpp
     void SynchronizationManager::RequestChangeVideo(const std::wstring& path) {
         auto msg = std::make_unique<PathMessage>(PathMessage{ path });
         if (m_pathQueue.Push(msg.get())) {
             msg.release(); // Transfer ownership to the queue only on success
         }
         // If Push fails, unique_ptr naturally deletes the message on scope exit.
     }
     ```
  2. **Value Semantics**: Redesign the queue to store `std::wstring` or `PathMessage` by value (using std::move) instead of raw pointers, eliminating heap allocation/deallocation overhead entirely.

---

## 2. Reference Leaks of `IMFSample*` in `VideoDecoder`
* **Target File**: `src/video_decoder.cpp` (Lines 234–259, 440–491), `src/video_decoder.h` (Line 75)
* **Direct Observation**:
  In `UpdateFrame()`:
  ```cpp
  IMFSample* poppedSample = nullptr;
  if (m_sampleQueue.Pop(poppedSample)) {
      pSelectedSample.Attach(poppedSample);
      hasNewFrame = true;
  }
  ```
  In `CloseVideo()`:
  ```cpp
  m_sampleQueue.Clear();
  ```
* **Root Cause**:
  1. **Queue Flush Lack on Loop**: When the background thread reaches the end of the video stream, it flushes the source reader and loops back to 0. However, `m_sampleQueue` is not cleared during looping, so it still contains old samples from the end of the video. The new samples decoded from the beginning are pushed behind them. This can fill the queue, causing new samples to be immediately discarded (called `pRawSample->Release()`).
  2. **Active Frame Dropping and Attach Reassignment**: In `UpdateFrame()`, multiple samples may be popped in a loop to catch up to the playback time. Each pop calls `pSelectedSample.Attach(poppedSample)`. Although WRL `ComPtr::Attach` releases the previous pointer, if the loop exits early or throws an exception after a sample is popped but before it is attached, the raw `IMFSample*` pointer is leaked.
  3. **Errors in LoadVideo**: If `LoadVideo()` fails midway after creating the source reader but before spawning the thread, the sample queue is not cleared, leaving stale samples from previous videos.
* **Fix Strategy**:
  1. **ComPtr in Queue**: Redesign `SPSCRingBuffer` to hold `Microsoft::WRL::ComPtr<IMFSample>` instead of raw pointers. This automatically decrements reference counts upon elements being overwritten, popped, or cleared without relying on manual `.Release()` calls.
  2. **Clear Queue on Loop**: When the decoder loops back to 0 in `DecodingThreadProc`, clear the sample queue to discard late frames from the previous iteration, ensuring immediate presentation of the new loop frames.
  3. **Robust Cleanup on Load Failure**: Call `CloseVideo()` in all failure paths of `LoadVideo()` to ensure that the sample queue and source reader are cleared.

---

## 3. Clean Destructor Behavior for Lock-Free Collections
* **Target File**: `src/spsc_ring_buffer.h` (Lines 88–96), `src/render_thread_controller.h` (Lines 44–58), `src/render_thread_controller.cpp` (Lines 12–14)
* **Direct Observation**:
  ```cpp
  ~SPSCRingBuffer() {
      Clear();
  }
  ```
* **Root Cause**:
  1. **Tightly Coupled Destructor**: `SPSCRingBuffer::Clear` assumes that its elements are pointers implementing a `.Release()` method. If a non-pointer or a type without a `.Release()` method is used, the code fails to compile.
  2. **Concurrency on Destruction**: If the manager (e.g. `VideoDecoder`) is destroyed while the background thread is still running, the background thread may attempt to access the collection during or after its destruction, causing a crash.
  3. **Ownership Lifetime Mismatch**: If `RenderThreadController` is destroyed before the render thread is joined, the member variables are destroyed in the reverse order of declaration. If the background thread is still active, it will access deleted structures.
* **Fix Strategy**:
  1. **RAII Wrapper**: Store resource-managing wrappers (like smart pointers or value types) in the lock-free collections so the compiler's default destructor automatically cleans them up without custom code.
  2. **Strict Joining Order**: Always signal the background threads to exit and join them *prior* to destroying the lock-free collections or dependent structures. In `RenderThreadController::~RenderThreadController()`, `Stop()` is called to join the render thread first. Similarly, `VideoDecoder::CloseVideo()` joins the decoder thread before clearing the queue.

---

## 4. Concurrency Access to `ID3D11DeviceContext`
* **Target File**: `src/video_decoder.cpp` (Lines 423–620), `live_wallpaper_rust/src/lib.rs` (Lines 269–335), `src/render_thread_controller.cpp`
* **Direct Observation**:
  `VideoDecoder::UpdateFrame` takes `ID3D11DeviceContext* pContext` and uses it to update textures. Rust's `ShaderHost::render` uses the immediate context to bind shaders and draw.
* **Root Cause**:
  1. **Immediate Context Single-Threading**: The Direct3D 11 Immediate Context is strictly single-threaded. Calling context commands concurrently from different threads results in undefined behavior or driver crashes.
  2. **Thread Boundaries**: In the codebase, only the Render Thread (`RenderThreadController::ThreadProc`) calls methods on the immediate context. The background decoder thread (`DecodingThreadProc`) only uses the Media Foundation Source Reader, which interacts with the `ID3D11Device` via the thread-safe `IMFDXGIDeviceManager`, and does not touch the immediate context.
  3. **Shared Device Access**: The D3D11 device itself is accessed concurrently by the render thread and Media Foundation decoder threads. While `SetMultithreadProtected(TRUE)` is enabled on the device, it adds locking overhead.
* **Fix Strategy**:
  1. **Strict Context Isolation**: Ensure that the immediate context is never passed to or called from the background decoder thread. (This is currently followed, but must be strictly enforced).
  2. **Deferred Contexts for Background Work**: If the background decoder thread ever needs to perform GPU commands, it must use a `Deferred Context` to record command lists, which are then executed on the immediate context from the render thread.
  3. **Device Multithread Protection**: Ensure that `D3D11_CREATE_DEVICE_SINGLETHREADED` is *not* used during device creation, and keep the multithread protection on the device enabled.

---

## 5. Thread-Safety of Texture Reallocations in Video Decoder
* **Target File**: `src/video_decoder.cpp` (Lines 270–299, 523–528)
* **Direct Observation**:
  ```cpp
  bool VideoDecoder::ReallocateVideoTexture(int width, int height) {
      m_pVideoSRV_Y.Reset();
      m_pVideoSRV_UV.Reset();
      m_pVideoTexture.Reset();
      // ... creates new textures and SRVs ...
  }
  ```
* **Root Cause**:
  1. **In-place Destructive Allocation**: `ReallocateVideoTexture` immediately resets the existing textures and SRVs before creating new ones.
  2. **Crash on Failure**: If creation of the new texture or SRVs fails, the function returns `false`. However, the active SRVs (`m_pActiveSRV_Y` and `m_pActiveSRV_UV`) still point to the old, now-destroyed shader resource views.
  3. **Stale Bindings**: If the render loop has `forceRedraw` set, it will attempt to render using these stale active SRVs, leading to GPU errors or crashes.
* **Fix Strategy**:
  1. **Transactional Allocation**: Create the new texture and SRVs in local temporary variables first. Only copy them to `m_pVideoTexture`, `m_pVideoSRV_Y`, and `m_pVideoSRV_UV` once creation has fully succeeded.
     ```cpp
     Microsoft::WRL::ComPtr<ID3D11Texture2D> pNewTexture;
     Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pNewSRV_Y;
     Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pNewSRV_UV;
     // ... allocate into new variables ...
     if (SUCCEEDED(hr)) {
         m_pVideoTexture = pNewTexture;
         m_pVideoSRV_Y = pNewSRV_Y;
         m_pVideoSRV_UV = pNewSRV_UV;
         return true;
     }
     ```
  2. **Clear Active SRVs on Failure**: If reallocation fails, clear the active SRVs to `nullptr` and update the renderer to skip drawing when SRVs are null.

---

## 6. Synchronization of Window Destruction and Recreation
* **Target File**: `src/explorer_integration.cpp` (Lines 201–246), `src/main.cpp` (Lines 342, 360–365)
* **Direct Observation**:
  `ExplorerIntegration::Update()` calls `Shutdown()` (destroying `m_hWnd`) and then `Initialize()` (creating a new window) synchronously on the main thread.
* **Root Cause**:
  1. **Unsynchronized Destruction**: The render thread continues to run and draw/present to the swap chain while `DestroyWindow(m_hWnd)` is called on the main thread.
  2. **Asynchronous Notification**: The main thread only requests the render thread to recreate the swap chain after the window has already been destroyed. This causes a race condition where the render thread accesses a dead window handle.
  3. **Exit on Startup Failure**: If `Initialize` fails on startup, the application exits immediately rather than retrying in the background.
* **Fix Strategy**:
  1. **Pre-destruction Handshake**: Before destroying `m_hWnd` in `ExplorerIntegration::Shutdown`, the main thread must call `renderThread.RequestRecreate(nullptr)` and wait/yield to let the render thread release its swap chain and detach from the window.
  2. **Graceful Startup**: On startup failure of `Initialize`, do not exit. Set `m_isRecovering = true` and allow the background watchdog loop in `Update()` to attempt initialization using the exponential backoff retry logic.

---

## 7. Caching Window Dimensions in Main Loop
* **Target File**: `src/main.cpp` (Lines 367–374), `src/swap_chain_manager.cpp` (Lines 166–185)
* **Direct Observation**:
  ```cpp
  if (currentHWnd && IsWindow(currentHWnd)) {
      RECT rect;
      if (GetClientRect(currentHWnd, &rect)) {
          int w = rect.right - rect.left;
          int h = rect.bottom - rect.top;
          renderThread.RequestResize(w, h);
      }
  }
  ```
* **Root Cause**:
  1. **Unconditional Resize Requests**: The main loop calls `RequestResize` on every iteration, setting the resize flag to `true` on the render thread.
  2. **GPU Stalls**: The render thread constantly calls `SwapChainManager::Resize`, which resets the render target view, calls `ResizeBuffers`, and recreates the view. This stalls the GPU pipeline, causing terrible performance and bypassing frame pacing.
* **Fix Strategy**:
  1. **Cache Dimensions**: Cache the width and height in the main message loop, and only call `RequestResize` when a size change is detected.
     ```cpp
     int cachedWidth = 0, cachedHeight = 0;
     // ...
     int w = rect.right - rect.left;
     int h = rect.bottom - rect.top;
     if (w != cachedWidth || h != cachedHeight) {
         renderThread.RequestResize(w, h);
         cachedWidth = w;
         cachedHeight = h;
     }
     ```

---

## 8. Shader Compilation Errors and Secure DLL Loading
* **Target File**: `live_wallpaper_rust/src/lib.rs` (Lines 123–140, 407–459), `src/ffi_shader_bridge.cpp` (Lines 10–29)
* **Direct Observation**:
  In Rust:
  ```rust
  let ps_blob = match compile_res {
      Ok(blob) => Some(blob),
      Err(e) => {
          eprintln!("[Rust Shader Host] Initial compilation error: {:?}", e);
          compile_shader(FALLBACK_PS_CODE, "main", "ps_4_0").ok()
      }
  };
  ```
* **Root Cause**:
  1. **Swallowed Errors**: Compilation errors are printed to standard error (which is invisible in a GUI application) and swallowed. The function returns a valid shader host using the fallback shader, returning `S_OK` to C++.
  2. **Unvalidated DLL Load**: `LoadLibraryExW` restricts DLL search paths, but does not verify the signature, hash, or ACLs of the loaded `live_wallpaper_rust.dll`, exposing it to local tampering.
* **Fix Strategy**:
  1. **Propagate Compilation Errors**: Modify the FFI signature of `init_shader_host` to accept an error output buffer. If compilation fails, copy the compiler diagnostic string from `error_blob` into this buffer and return an error HRESULT.
  2. **DLL Signature & Integrity Checks**: Verify the DLL using the Win32 `WinVerifyTrust` API or verify its SHA-256 hash against a known signed manifest. Lock the directory ACLs to prevent non-admin modifications.
