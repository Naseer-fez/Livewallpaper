# Progress

Last visited: 2026-07-15T22:30:00Z

- [x] Search the repository to understand codebase structure and locate target files.
- [x] Implement smart pointer logic for PathMessage (unique_ptr) in SynchronizationManager and Queue.
- [x] Implement IMFSample ComPtr in VideoDecoder and RingBuffer, flushing in DecodingThreadProc, CloseVideo in failure paths of LoadVideo.
- [x] Update SPSCRingBuffer destructor to support move-only types and release pointers.
- [x] Utilize D3D11 multithread lock (ID3D10Multithread) in RenderThreadController's render loop.
- [x] Rewrite ReallocateVideoTexture to use local temporary ComPtrs.
- [x] Refactor recovery logic: NeedsRecovery(), main loop integration, exponential backoff, poll renderThread.IsDetached(), requestRecreate(nullptr), requestRecreate(host.GetHWND()).
- [x] Optimize resize polling and SynchronizationManager::RequestResize redundancy checks.
- [x] Propagate Rust HLSL compile errors to C++ via FFI signature update and UTF-16 conversion.
- [x] Add security checks for absolute DLL path, exists, no '..' traversal in ffi_shader_bridge.cpp.
- [/] Build & run tests (in-progress).
