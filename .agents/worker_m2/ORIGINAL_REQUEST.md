## 2026-07-15T21:38:25Z
You are the Worker. Your working directory is d:\CODE\Utlities\LiveWallpaper\.agents\worker_m2.
Your objective is to modify the LiveWallpaper codebase at d:\CODE\Utlities\LiveWallpaper to resolve the follow-up requirements:
1. Heap memory leaks of PathMessage* in SynchronizationManager::RequestChangeVideo and RequestAddVideo (if queue push fails) by adopting smart pointers (std::unique_ptr<PathMessage>) inside the queue and SynchronizationManager.
2. Reference leaks of IMFSample* in VideoDecoder::UpdateFrame and CloseVideo when samples are discarded or left in SPSCRingBuffer by adopting Microsoft::WRL::ComPtr<IMFSample> inside the queue, flushing the queue on video loop (DecodingThreadProc), and calling CloseVideo in failure paths of LoadVideo.
3. Clean destructor behavior for lock-free collections (SPSCRingBuffer) by supporting move-only types (using std::move, std::is_pointer_v logic) so smart pointers are automatically released.
4. Concurrency access to ID3D11DeviceContext between the render thread (RenderThreadController) and background decoder thread by utilizing the D3D11 multithread lock (ID3D10Multithread) Enter/Leave inside RenderThreadController's render loop (wrapping UpdateFrame, RenderVideoFrame, RenderShaderFrame, and Present calls).
5. Thread-safety of texture reallocations in the video decoder by rewriting ReallocateVideoTexture to allocate new textures and SRVs into local temporary ComPtrs, only updating the member variables if all allocations succeed.
6. Synchronized window destruction and recreation in ExplorerIntegration during recovery:
   - Expose NeedsRecovery() from ExplorerIntegration.
   - Move the recovery logic to main.cpp's message loop, using exponential backoff (starting at 1s, doubling up to 30s).
   - In main.cpp, before calling host.Shutdown(), call renderThread.RequestRecreate(nullptr) and poll renderThread.IsDetached() (by exposing an IsDetached method and m_isDetached atomic flag on SynchronizationManager/RenderThreadController) for up to 1000ms to ensure D3D11 resources are detached before window destruction.
   - If recovery succeeds, call renderThread.RequestRecreate(host.GetHWND()).
   - Do not exit on startup failure in main.cpp if Initialize fails; let the app run and retry in the background via the watchdog.
7. Optimize resize polling in main message pump:
   - Cache window dimensions (lastWidth/lastHeight) in main.cpp and only call renderThread.RequestResize when dimensions actually change.
   - Prevent redundant resize flags in SynchronizationManager::RequestResize by matching against current requested width/height.
8. Propagate HLSL compilation errors from Rust to C++:
   - Update init_shader_host FFI signature in Rust (lib.rs) and C++ (ffi_shader_bridge) to accept error buffer parameters (u16* out_error_buffer, u32 error_buffer_len).
   - If compilation fails in ShaderHost::new, return the error instead of falling back. init_shader_host should write the error string to out_error_buffer (encode UTF-16) and return the compilation error HRESULT.
   - Update Rust unit tests in lib.rs to match the new signature of init_shader_host.
   - In ffi_shader_bridge.cpp, add security checks verifying the DLL path is absolute, exists, and has no '..' traversal components before LoadLibraryExW.

Output Requirements:
- Build the project using `build.bat`.
- Run C++ unit tests: `build\tests\Release\LiveWallpaperTests.exe` or `build\tests\Debug\LiveWallpaperTests.exe` (or check how to invoke them).
- Run Python E2E tests: `python tests/e2e_test_runner.py`.
- Ensure all builds succeed and all tests pass 100%.
- Write a handoff report to d:\CODE\Utlities\LiveWallpaper\.agents\worker_m2\handoff.md describing all code changes and the build/test outcomes.
