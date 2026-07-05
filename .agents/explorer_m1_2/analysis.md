# Analysis: Render Pipeline Diagnostic Instrumentation (Requirement R1)

## 1. Overview and Instrumentation Strategy
To satisfy **Requirement R1**, we must add comprehensive diagnostic logging throughout the render pipeline of the Live Wallpaper Engine. The goal is to produce log files that make it trivial to perform side-by-side comparisons between a working developer environment and a failing target system. 

To achieve this, we will use the application's existing logging macro suite defined in `src/utils.h`:
- `LOG_INFO(fmt, ...)` and `LOG_INFO_W(fmt, ...)` for pipeline state transitions, initialization success, and key milestones.
- `LOG_WARN(fmt, ...)` and `LOG_WARN_W(fmt, ...)` for soft fallbacks or recoverable errors.
- `LOG_ERROR(fmt, ...)` and `LOG_ERROR_W(fmt, ...)` for unrecoverable errors.
- `LOG_DEBUG(fmt, ...)` and `LOG_DEBUG_W(fmt, ...)` for high-frequency frame loop diagnostics (e.g., individual frame present HRESULTs) to avoid performance degradation and log bloat in normal execution.

Every stage is instrumented with:
1. **Entry Logging**: Verifying execution reached the stage.
2. **Contextual Metadata**: Log window handles, rect dimensions, feature levels, GPU adapter details, and video codec GUIDs.
3. **Success/Failure with HRESULTs**: Checking and logging all return values using standard hexadecimal formatting (`0x%08X`).

---

## 2. File-by-File Diagnostic Logging Design

Below is the detailed specification of which files must be modified, where, and the exact logging statements to add.

### File 1: `src/main.cpp`
- **Purpose**: Instrument application startup and the main thread COM library initialization.
- **Location**: Inside `WinMain` around line 68–74.
- **Proposed Code Changes**:
  ```cpp
  // Before CoInitializeEx:
  LOG_INFO("WinMain starting. CmdLine: '%s'", lpCmdLine);
  
  LOG_INFO("WinMain: Initializing COM library (COINIT_APARTMENTTHREADED)...");
  HRESULT hrCOM = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  LOG_INFO("WinMain: CoInitializeEx completed. HRESULT = 0x%08X", hrCOM);
  if (FAILED(hrCOM)) {
      LOG_ERROR("CoInitializeEx failed in main thread. HRESULT: 0x%08X", hrCOM);
  }
  ```

---

### File 2: `src/explorer_integration.cpp`
- **Purpose**: Instrument the desktop injection process: discovering `WorkerW`, creating the host window, and attaching it.
- **Location 1: `ExplorerIntegration::Initialize` (line 10)**
  ```cpp
  bool ExplorerIntegration::Initialize(HINSTANCE hInstance) {
      m_hInstance = hInstance;
      m_isShuttingDown.store(false);
      LOG_INFO("ExplorerIntegration::Initialize entry. hInstance = %p", hInstance);

      if (!FindWorkerW()) {
          LOG_ERROR("ExplorerIntegration::Initialize: FindWorkerW failed.");
          return false;
      }

      if (!CreateHostWindow(hInstance)) {
          LOG_ERROR("ExplorerIntegration::Initialize: CreateHostWindow failed.");
          return false;
      }

      if (!InjectIntoDesktop()) {
          LOG_ERROR("ExplorerIntegration::Initialize: InjectIntoDesktop failed.");
          return false;
      }

      m_lastUpdateTick = GetTickCount();
      LOG_INFO("ExplorerIntegration::Initialize: Success. Host HWND = %p, WorkerW HWND = %p", m_hWnd, m_hWorkerW);
      return true;
  }
  ```

- **Location 2: `ExplorerIntegration::FindWorkerW` (line 50)**
  ```cpp
  bool ExplorerIntegration::FindWorkerW() {
      LOG_INFO("ExplorerIntegration::FindWorkerW entry.");
      HWND progman = FindWindowW(L"Progman", NULL);
      LOG_INFO("FindWorkerW: FindWindowW('Progman') result = %p", progman);
      if (!progman) {
          LOG_ERROR("Progman window not found.");
          return false;
      }
      
      LOG_INFO("FindWorkerW: Sending 0x052C to Progman...");
      ULONG_PTR result = 0;
      LRESULT sendResult = SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_ABORTIFHUNG, 1000, &result);
      LOG_INFO("FindWorkerW: SendMessageTimeoutW(0x052C) returned %ld, result = 0x%p", sendResult, (void*)result);

      HWND shellDefView = NULL;
      HWND parentOfShell = NULL;
      HWND wallpaperWorkerW = NULL;

      shellDefView = FindWindowExW(progman, NULL, L"SHELLDLL_DefView", NULL);
      if (shellDefView) {
          parentOfShell = progman;
          LOG_INFO("FindWorkerW: SHELLDLL_DefView found in Progman: %p", progman);
      } else {
          HWND workerW = FindWindowExW(NULL, NULL, L"WorkerW", NULL);
          while (workerW) {
              LOG_DEBUG("FindWorkerW: Enumerating WorkerW = %p", workerW);
              shellDefView = FindWindowExW(workerW, NULL, L"SHELLDLL_DefView", NULL);
              if (shellDefView) {
                  parentOfShell = workerW;
                  LOG_INFO("FindWorkerW: SHELLDLL_DefView found in WorkerW parent: %p", workerW);
                  break;
              }
              workerW = FindWindowExW(NULL, workerW, L"WorkerW", NULL);
          }
      }

      if (!shellDefView) {
          LOG_ERROR("FindWorkerW: SHELLDLL_DefView not found anywhere on the system.");
          return false;
      }

      if (parentOfShell == progman) {
          m_hWorkerW = progman;
          m_hShellDefView = shellDefView;
          m_useLegacyWorkerW = false;
          LOG_WARN("FindWorkerW: Fallback to Progman triggered. parentOfShell == progman. Target HWND = %p", m_hWorkerW);
      } else {
          HWND workerW = FindWindowExW(NULL, NULL, L"WorkerW", NULL);
          while (workerW) {
              LOG_DEBUG("FindWorkerW: Enumerating WorkerW (Pass 2) = %p", workerW);
              if (workerW != parentOfShell && !FindWindowExW(workerW, NULL, L"SHELLDLL_DefView", NULL)) {
                  wallpaperWorkerW = workerW;
                  LOG_INFO("FindWorkerW: Found empty WorkerW for wallpaper injection: %p", workerW);
                  break;
              }
              workerW = FindWindowExW(NULL, workerW, L"WorkerW", NULL);
          }

          if (wallpaperWorkerW) {
              m_hWorkerW = wallpaperWorkerW;
              m_hShellDefView = NULL;
              m_useLegacyWorkerW = true;
              LOG_INFO("FindWorkerW: Dedicated wallpaper WorkerW assigned: %p", m_hWorkerW);
          } else {
              m_hWorkerW = progman;
              m_hShellDefView = NULL;
              m_useLegacyWorkerW = false;
              LOG_WARN("FindWorkerW: Fallback to Progman triggered (no empty WorkerW). Target HWND = %p", m_hWorkerW);
          }
      }

      LOG_INFO("FindWorkerW complete. Target m_hWorkerW = %p", m_hWorkerW);
      return m_hWorkerW != nullptr;
  }
  ```

- **Location 3: `ExplorerIntegration::CreateHostWindow` (line 124)**
  ```cpp
  bool ExplorerIntegration::CreateHostWindow(HINSTANCE hInstance) {
      LOG_INFO("ExplorerIntegration::CreateHostWindow entry.");
      WNDCLASSEXW wcx = { 0 };
      if (!GetClassInfoExW(hInstance, L"LiveWallpaperHostClass", &wcx)) {
          wcx.cbSize = sizeof(wcx);
          wcx.style = CS_HREDRAW | CS_VREDRAW;
          wcx.lpfnWndProc = WndProc;
          wcx.hInstance = hInstance;
          wcx.lpszClassName = L"LiveWallpaperHostClass";
          wcx.hCursor = LoadCursorW(NULL, IDC_ARROW);
          wcx.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
          if (!RegisterClassExW(&wcx)) {
              LOG_ERROR("CreateHostWindow: Failed to register window class 'LiveWallpaperHostClass'. Error: %d", GetLastError());
              return false;
          }
          LOG_INFO("CreateHostWindow: Registered window class 'LiveWallpaperHostClass'.");
      }

      int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
      int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
      int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
      int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);
      LOG_INFO("CreateHostWindow: Virtual screen metrics: x=%d, y=%d, width=%d, height=%d", x, y, cx, cy);

      m_hWnd = CreateWindowExW(
          WS_EX_NOACTIVATE, L"LiveWallpaperHostClass", L"LiveWallpaperHost",
          WS_CHILD | WS_VISIBLE, x, y, cx, cy, m_hWorkerW, NULL, hInstance, this
      );

      LOG_INFO("CreateHostWindow: CreateWindowExW returned HWND = %p (Parent: %p). Error: %d", m_hWnd, m_hWorkerW, GetLastError());
      return m_hWnd != nullptr;
  }
  ```

- **Location 4: `ExplorerIntegration::InjectIntoDesktop` (line 153)**
  ```cpp
  bool ExplorerIntegration::InjectIntoDesktop() {
      LOG_INFO("ExplorerIntegration::InjectIntoDesktop entry.");
      if (!m_hWnd || !m_hWorkerW) {
          LOG_ERROR("InjectIntoDesktop: Host HWND (%p) or WorkerW HWND (%p) is null.", m_hWnd, m_hWorkerW);
          return false;
      }

      HWND currentParent = GetParent(m_hWnd);
      LOG_INFO("InjectIntoDesktop: Current parent is %p (Expected: %p)", currentParent, m_hWorkerW);
      if (currentParent != m_hWorkerW) {
          HWND prevParent = SetParent(m_hWnd, m_hWorkerW);
          LOG_INFO("InjectIntoDesktop: SetParent completed. Previous parent = %p, New parent = %p", prevParent, GetParent(m_hWnd));
      }

      int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
      int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
      int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
      int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);

      HWND hWndInsertAfter = HWND_BOTTOM;
      if (!m_useLegacyWorkerW && m_hShellDefView) {
          hWndInsertAfter = m_hShellDefView;
      }
      LOG_INFO("InjectIntoDesktop: Arranging host window position: After = %p, x=%d, y=%d, cx=%d, cy=%d", hWndInsertAfter, x, y, cx, cy);

      BOOL posResult = SetWindowPos(m_hWnd, hWndInsertAfter, x, y, cx, cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
      LOG_INFO("InjectIntoDesktop: SetWindowPos completed. Result = %d", posResult);

      HWND finalParent = GetParent(m_hWnd);
      LOG_INFO("InjectIntoDesktop: Final injected state verification: HWND = %p, Parent = %p (Expected: %p)", m_hWnd, finalParent, m_hWorkerW);
      return true;
  }
  ```

---

### File 3: `src/render_thread_controller.h` and `src/render_thread_controller.cpp`
- **Purpose**: Instrument COM initialization inside the render thread, and log the first frame milestone.
- **Proposed Changes in `src/render_thread_controller.h`**:
  Add `m_firstFrameMilestoneLogged` to track the first successful decode & present:
  ```cpp
  private:
      // ... existing code ...
      bool m_firstFrameMilestoneLogged = false;
  ```

- **Proposed Changes in `src/render_thread_controller.cpp`**:
  - **Inside `ThreadProc` (line 82)**:
    ```cpp
    void RenderThreadController::ThreadProc() {
        LOG_INFO("RenderThreadController::ThreadProc entry.");
        m_screenCleared = false;
        m_firstFrameMilestoneLogged = false;

        LOG_INFO("ThreadProc: Initializing multi-threaded COM library (COINIT_MULTITHREADED)...");
        HRESULT hrCOM = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        LOG_INFO("ThreadProc: CoInitializeEx completed. HRESULT = 0x%08X", hrCOM);
        if (FAILED(hrCOM)) {
            LOG_ERROR("CoInitializeEx failed in RenderThreadController thread. HRESULT: 0x%08X", hrCOM);
        }
        
        // ... instantiation of components ...
    ```

  - **Inside the loop in `ThreadProc` (around line 301–321) where video frame rendering occurs**:
    ```cpp
                } else if (m_decoder->IsVideoLoaded()) {
                    double waitTimeMs = 0.0;
                    frameUpdated = m_decoder->UpdateFrame(m_deviceManager->GetContext(), waitTimeMs);

                    if (frameUpdated || forceRedraw) {
                        m_videoRenderer->RenderVideoFrame(
                            m_decoder->GetSRV_Y(),
                            m_decoder->GetSRV_UV(),
                            m_decoder->GetTextureWidth(),
                            m_decoder->GetTextureHeight(),
                            m_decoder->GetVideoWidth(),
                            m_decoder->GetVideoHeight()
                        );
                        hrPresent = m_swapChainManager->Present(m_syncManager->GetFPSLimit());
                        
                        // Milestone Logging: First Frame Presented
                        if (frameUpdated && SUCCEEDED(hrPresent) && !m_firstFrameMilestoneLogged) {
                            LOG_INFO("MILESTONE: First video frame successfully decoded AND presented to desktop. TimeTick: %llu", GetTickCount64());
                            m_firstFrameMilestoneLogged = true;
                        }
                    } else {
                        if (waitTimeMs > 0.0) {
                            Timer::PreciseSleep(waitTimeMs < 1.0 ? 1.0 : waitTimeMs);
                        }
                    }
                }
    ```

---

### File 4: `src/device_manager.cpp`
- **Purpose**: Log device creation details, HRESULT fallback, feature levels, and GPU adapter properties.
- **Proposed Changes in `DeviceManager::Initialize` (line 10)**:
  ```cpp
  bool DeviceManager::Initialize() {
      LOG_INFO("DeviceManager::Initialize entry.");

      D3D_FEATURE_LEVEL featureLevels[] = {
          D3D_FEATURE_LEVEL_11_1,
          D3D_FEATURE_LEVEL_11_0,
          D3D_FEATURE_LEVEL_10_1,
          D3D_FEATURE_LEVEL_10_0
      };
      D3D_FEATURE_LEVEL supportedLevel;
      UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT; 
  #ifdef _DEBUG
      creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
  #endif
      
      LOG_INFO("DeviceManager::Initialize: Attempting Hardware D3D11 device creation. Flags = 0x%08X", creationFlags);

      HRESULT hr = D3D11CreateDevice(
          NULL,
          D3D_DRIVER_TYPE_HARDWARE,
          NULL,
          creationFlags,
          featureLevels,
          ARRAYSIZE(featureLevels),
          D3D11_SDK_VERSION,
          &m_d3dDevice,
          &supportedLevel,
          &m_d3dContext
      );

      LOG_INFO("DeviceManager::Initialize: Hardware device creation outcome: HRESULT = 0x%08X, Supported Feature Level = 0x%04X", hr, supportedLevel);

      if (FAILED(hr)) {
          LOG_WARN("Hardware D3D11 Device creation failed (HRESULT: 0x%08X). Falling back to WARP driver...", hr);
          hr = D3D11CreateDevice(
              NULL,
              D3D_DRIVER_TYPE_WARP,
              NULL,
              creationFlags,
              featureLevels,
              ARRAYSIZE(featureLevels),
              D3D11_SDK_VERSION,
              &m_d3dDevice,
              &supportedLevel,
              &m_d3dContext
          );
          LOG_INFO("DeviceManager::Initialize: WARP device creation outcome: HRESULT = 0x%08X, Supported Feature Level = 0x%04X", hr, supportedLevel);
      }

      if (FAILED(hr)) {
          LOG_ERROR("DeviceManager::Initialize: Both Hardware and WARP D3D11 Device creation failed. HRESULT = 0x%08X", hr);
          return false;
      }

      // Log Adapter details
      Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
      HRESULT hrDiag = m_d3dDevice.As(&dxgiDevice);
      if (SUCCEEDED(hrDiag)) {
          Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
          hrDiag = dxgiDevice->GetAdapter(&dxgiAdapter);
          if (SUCCEEDED(hrDiag)) {
              DXGI_ADAPTER_DESC desc = {};
              hrDiag = dxgiAdapter->GetDesc(&desc);
              if (SUCCEEDED(hrDiag)) {
                  LOG_INFO_W(L"DeviceManager::Initialize: Active Adapter: %ls (VendorID: 0x%04X, DeviceID: 0x%04X, VideoMemory: %llu MB)",
                      desc.Description, desc.VendorId, desc.DeviceId, (unsigned long long)(desc.DedicatedVideoMemory / (1024 * 1024)));
              }
          }
      }

      // Protect context for multithreading (needed by Media Foundation)
      Microsoft::WRL::ComPtr<ID3D10Multithread> pMultithread;
      HRESULT hrMT = m_d3dDevice.As(&pMultithread);
      LOG_INFO("DeviceManager::Initialize: QueryInterface ID3D10Multithread result = 0x%08X", hrMT);
      if (SUCCEEDED(hrMT)) {
          pMultithread->SetMultithreadProtected(TRUE);
          LOG_INFO("DeviceManager::Initialize: Context multithreading protection enabled.");
      } else {
          LOG_WARN("DeviceManager::Initialize: Failed to enable multithreading protection.");
      }

      LOG_INFO("DeviceManager successfully initialized.");
      return true;
  }
  ```

---

### File 5: `src/swap_chain_manager.cpp`
- **Purpose**: Instrument SwapChain and RenderTargetView (RTV) setup, resize operations, and Present HRESULTs.
- **Proposed Changes**:
  - **Inside `Initialize` (line 10)**:
    ```cpp
    bool SwapChainManager::Initialize(ID3D11Device* device, HWND hWnd, int width, int height) {
        m_hWnd = hWnd;
        m_width = width;
        m_height = height;
        LOG_INFO("SwapChainManager::Initialize entry. HWND = %p, width = %d, height = %d", hWnd, width, height);

        if (!CreateSwapChain(device)) {
            LOG_ERROR("SwapChainManager::Initialize: CreateSwapChain failed.");
            Shutdown();
            return false;
        }

        if (!CreateRenderTargetView(device)) {
            LOG_ERROR("SwapChainManager::Initialize: CreateRenderTargetView failed.");
            Shutdown();
            return false;
        }

        LOG_INFO("SwapChainManager successfully initialized.");
        return true;
    }
    ```

  - **Inside `CreateSwapChain` (line 42)**:
    ```cpp
    bool SwapChainManager::CreateSwapChain(ID3D11Device* device) {
        LOG_INFO("SwapChainManager::CreateSwapChain entry.");
        if (!device) {
            LOG_ERROR("CreateSwapChain: D3D11 device is null.");
            return false;
        }

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        LOG_INFO("CreateSwapChain: QueryInterface IDXGIDevice result = 0x%08X", hr);
        if (FAILED(hr)) return false;

        Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
        LOG_INFO("CreateSwapChain: GetAdapter result = 0x%08X", hr);
        if (FAILED(hr)) return false;

        Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
        hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
        LOG_INFO("CreateSwapChain: GetParent IDXGIFactory2 result = 0x%08X", hr);
        if (FAILED(hr)) return false;

        DXGI_SWAP_CHAIN_DESC1 scd = { 0 };
        scd.Width = m_width;
        scd.Height = m_height;
        scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.SampleDesc.Count = 1;
        scd.SampleDesc.Quality = 0;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 2;
        scd.Scaling = DXGI_SCALING_STRETCH;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsd = { 0 };
        fsd.RefreshRate.Numerator = 60;
        fsd.RefreshRate.Denominator = 1;
        fsd.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        fsd.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        fsd.Windowed = TRUE;

        LOG_INFO("CreateSwapChain: Calling CreateSwapChainForHwnd. HWND = %p, Dimensions = %dx%d", m_hWnd, m_width, m_height);
        hr = dxgiFactory->CreateSwapChainForHwnd(
            device,
            m_hWnd,
            &scd,
            &fsd,
            NULL,
            &m_swapChain
        );
        LOG_INFO("CreateSwapChain: CreateSwapChainForHwnd result = 0x%08X", hr);

        if (FAILED(hr)) {
            LOG_ERROR("SwapChainManager: CreateSwapChainForHwnd failed. HRESULT = 0x%08X", hr);
            return false;
        }
        return true;
    }
    ```

  - **Inside `CreateRenderTargetView` (line 93)**:
    ```cpp
    bool SwapChainManager::CreateRenderTargetView(ID3D11Device* device) {
        LOG_INFO("SwapChainManager::CreateRenderTargetView entry.");
        if (!device || !m_swapChain) {
            LOG_ERROR("CreateRenderTargetView: Device (%p) or SwapChain (%p) is null.", device, m_swapChain.Get());
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        LOG_INFO("CreateRenderTargetView: GetBuffer(0) result = 0x%08X", hr);
        if (FAILED(hr)) return false;

        hr = device->CreateRenderTargetView(backBuffer.Get(), NULL, &m_renderTargetView);
        LOG_INFO("CreateRenderTargetView: CreateRenderTargetView result = 0x%08X", hr);
        if (FAILED(hr)) return false;

        return true;
    }
    ```

  - **Inside `Resize` (line 106)**:
    ```cpp
    bool SwapChainManager::Resize(ID3D11Device* device, ID3D11DeviceContext* context, int width, int height) {
        if (width <= 0 || height <= 0) return false;
        if (width == m_width && height == m_height) return true;

        LOG_INFO("Resizing SwapChain from %dx%d to %dx%d...", m_width, m_height, width, height);
        m_width = width;
        m_height = height;

        if (!m_swapChain) {
            LOG_ERROR("Resize: SwapChain is null.");
            return false;
        }

        if (context) {
            context->OMSetRenderTargets(0, nullptr, nullptr);
            context->ClearState();
            context->Flush();
        }
        m_renderTargetView.Reset();

        HRESULT hr = m_swapChain->ResizeBuffers(
            2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0
        );
        LOG_INFO("Resize: SwapChain->ResizeBuffers result = 0x%08X", hr);

        if (FAILED(hr)) {
            LOG_ERROR("SwapChainManager: ResizeBuffers failed. HRESULT = 0x%08X", hr);
            return false;
        }

        return CreateRenderTargetView(device);
    }
    ```

  - **Inside `Present` (line 136)**:
    ```cpp
    HRESULT SwapChainManager::Present(int fpsLimit) {
        if (!m_swapChain) return E_POINTER;
        UINT syncInterval = (fpsLimit == 0) ? 1 : 0;
        HRESULT hr = m_swapChain->Present(syncInterval, 0);
        
        // Use debug logging for high-frequency frame presentations to avoid spamming the log.
        if (FAILED(hr)) {
            LOG_ERROR("SwapChainManager::Present failed. HRESULT = 0x%08X", hr);
        } else {
            LOG_DEBUG("SwapChainManager::Present succeeded. HRESULT = 0x%08X", hr);
        }
        return hr;
    }
    ```

---

### File 6: `src/video_decoder.cpp`
- **Purpose**: Instrument Media Foundation startup, the DXGI device manager setup, the 4-step loading fallback mechanism, codec identification, frame validation, and GPU-vs-CPU copy path selections.
- **Proposed Changes**:
  - **Inside `Initialize` (line 12)**:
    ```cpp
    bool VideoDecoder::Initialize(ID3D11Device* pDevice) {
        LOG_INFO("VideoDecoder::Initialize entry.");
        if (!pDevice) {
            LOG_ERROR("VideoDecoder::Initialize received null D3D11 device.");
            return false;
        }
        m_pDevice = pDevice;

        Microsoft::WRL::ComPtr<ID3D10Multithread> pMultithread;
        HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pMultithread));
        LOG_INFO("VideoDecoder::Initialize: QueryInterface ID3D10Multithread result = 0x%08X", hr);
        if (SUCCEEDED(hr)) {
            pMultithread->SetMultithreadProtected(TRUE);
            LOG_INFO("VideoDecoder::Initialize: Context multithreading protection enabled.");
        } else {
            LOG_WARN("VideoDecoder::Initialize: Failed to enable D3D11 multithread protection.");
        }

        hr = MFStartup(MF_VERSION);
        LOG_INFO("VideoDecoder::Initialize: MFStartup result = 0x%08X", hr);
        if (FAILED(hr)) {
            LOG_ERROR("VideoDecoder::Initialize: MFStartup failed. HRESULT: 0x%08X", hr);
            return false;
        }

        hr = MFCreateDXGIDeviceManager(&m_deviceResetToken, &m_pDeviceManager);
        LOG_INFO("VideoDecoder::Initialize: MFCreateDXGIDeviceManager result = 0x%08X, ResetToken = %u", hr, m_deviceResetToken);
        if (FAILED(hr)) {
            LOG_ERROR("VideoDecoder::Initialize: MFCreateDXGIDeviceManager failed. HRESULT: 0x%08X", hr);
            MFShutdown();
            return false;
        }

        hr = m_pDeviceManager->ResetDevice(m_pDevice, m_deviceResetToken);
        LOG_INFO("VideoDecoder::Initialize: IMFDXGIDeviceManager::ResetDevice result = 0x%08X", hr);
        if (FAILED(hr)) {
            LOG_ERROR("VideoDecoder::Initialize: IMFDXGIDeviceManager::ResetDevice failed. HRESULT: 0x%08X", hr);
            m_pDeviceManager.Reset();
            MFShutdown();
            return false;
        }

        LOG_INFO("VideoDecoder initialized successfully with DXVA2/D3D11 hardware acceleration.");
        return true;
    }
    ```

  - **Inside `LoadVideo` (line 64)**:
    We must add clear, per-attempt logging for all 4 fallback configurations:
    ```cpp
    bool VideoDecoder::LoadVideo(const std::wstring& filePath) {
        CloseVideo();
        m_filePath = filePath;
        LOG_INFO_W(L"VideoDecoder::LoadVideo entry. FilePath = %ls", filePath.c_str());

        struct FallbackOption {
            const char* name;
            bool useD3DManager;
            bool useVideoProcessing;
        };

        FallbackOption options[] = {
            { "D3D Manager + Video Processing", true, true },
            { "D3D Manager Only", true, false },
            { "Software + Video Processing", false, true },
            { "Software (No Attributes)", false, false }
        };

        bool initialized = false;
        HRESULT hr = E_FAIL;

        for (int i = 0; i < 4; ++i) {
            const auto& opt = options[i];
            LOG_INFO("LoadVideo: Attempting fallback combination %d/4: '%s'", i + 1, opt.name);
            
            if (opt.useD3DManager && !m_pDeviceManager) {
                LOG_WARN("LoadVideo: Skipping '%s' (Device Manager not initialized).", opt.name);
                continue;
            }

            Microsoft::WRL::ComPtr<IMFAttributes> pAttributes;
            UINT32 attrCount = 0;
            if (opt.useD3DManager) attrCount++;
            if (opt.useVideoProcessing) attrCount++;

            if (attrCount > 0) {
                hr = MFCreateAttributes(&pAttributes, attrCount);
                LOG_INFO("LoadVideo: MFCreateAttributes result = 0x%08X", hr);
                if (FAILED(hr)) {
                    LOG_WARN("LoadVideo: MFCreateAttributes failed. HRESULT: 0x%08X", hr);
                    continue;
                }
                if (opt.useD3DManager) {
                    hr = pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_pDeviceManager.Get());
                    LOG_DEBUG("LoadVideo: Attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER) result = 0x%08X", hr);
                }
                if (opt.useVideoProcessing) {
                    hr = pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
                    LOG_DEBUG("LoadVideo: Attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING) result = 0x%08X", hr);
                }
            }

            hr = MFCreateSourceReaderFromURL(m_filePath.c_str(), pAttributes.Get(), &m_pSourceReader);
            LOG_INFO("LoadVideo: MFCreateSourceReaderFromURL result = 0x%08X", hr);
            if (FAILED(hr)) {
                LOG_WARN("LoadVideo: MFCreateSourceReaderFromURL failed for '%s'. HRESULT: 0x%08X", opt.name, hr);
                m_pSourceReader.Reset();
                continue;
            }

            hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
            LOG_DEBUG("LoadVideo: SetStreamSelection(ALL_STREAMS, FALSE) result = 0x%08X", hr);
            if (FAILED(hr)) {
                m_pSourceReader.Reset();
                continue;
            }

            hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
            LOG_DEBUG("LoadVideo: SetStreamSelection(FIRST_VIDEO_STREAM, TRUE) result = 0x%08X", hr);
            if (FAILED(hr)) {
                m_pSourceReader.Reset();
                continue;
            }

            Microsoft::WRL::ComPtr<IMFMediaType> pType;
            hr = MFCreateMediaType(&pType);
            LOG_DEBUG("LoadVideo: MFCreateMediaType result = 0x%08X", hr);
            if (FAILED(hr)) {
                m_pSourceReader.Reset();
                continue;
            }

            hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            LOG_DEBUG("LoadVideo: SetGUID(MF_MT_MAJOR_TYPE) result = 0x%08X", hr);
            if (FAILED(hr)) {
                m_pSourceReader.Reset();
                continue;
            }

            hr = pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
            LOG_DEBUG("LoadVideo: SetGUID(MF_MT_SUBTYPE, NV12) result = 0x%08X", hr);
            if (FAILED(hr)) {
                m_pSourceReader.Reset();
                continue;
            }

            hr = m_pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType.Get());
            LOG_INFO("LoadVideo: SetCurrentMediaType(NV12) result = 0x%08X", hr);
            if (FAILED(hr)) {
                m_pSourceReader.Reset();
                continue;
            }

            LOG_INFO("LoadVideo: Success using combination '%s'", opt.name);
            initialized = true;
            break;
        }

        if (!initialized || !m_pSourceReader) {
            LOG_ERROR_W(L"LoadVideo: All Source Reader creation attempts failed for path: %ls", m_filePath.c_str());
            return false;
        }

        // Retrieve video width, height, and codec selection diagnostics
        Microsoft::WRL::ComPtr<IMFMediaType> pCurrentType;
        hr = m_pSourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType);
        LOG_INFO("LoadVideo: GetCurrentMediaType result = 0x%08X", hr);
        if (FAILED(hr)) {
            LOG_ERROR("LoadVideo: GetCurrentMediaType failed. HRESULT: 0x%08X", hr);
            return false;
        }

        UINT32 width = 0, height = 0;
        hr = MFGetAttributeSize(pCurrentType.Get(), MF_MT_FRAME_SIZE, &width, &height);
        LOG_INFO("LoadVideo: MFGetAttributeSize result = 0x%08X. Width = %u, Height = %u", hr, width, height);
        if (FAILED(hr)) {
            LOG_ERROR("LoadVideo: MFGetAttributeSize failed. HRESULT: 0x%08X", hr);
            return false;
        }

        m_videoWidth = width;
        m_videoHeight = height;

        // Log Codec Selection / Video Subtype details
        GUID subtype = { 0 };
        if (SUCCEEDED(pCurrentType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
            if (subtype == MFVideoFormat_H264) {
                LOG_INFO("LoadVideo: Codec subtype matched: H.264 / AVC");
            } else if (subtype == MFVideoFormat_HEVC) {
                LOG_INFO("LoadVideo: Codec subtype matched: H.265 / HEVC");
            } else if (subtype == MFVideoFormat_WMV3) {
                LOG_INFO("LoadVideo: Codec subtype matched: WMV3");
            } else {
                LOG_INFO("LoadVideo: Codec subtype GUID: {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", 
                    subtype.Data1, subtype.Data2, subtype.Data3, 
                    subtype.Data4[0], subtype.Data4[1], subtype.Data4[2], subtype.Data4[3], 
                    subtype.Data4[4], subtype.Data4[5], subtype.Data4[6], subtype.Data4[7]);
            }
        }

        LOG_INFO_W(L"LoadVideo: Loaded video dimensions: %ls (%dx%d)", m_filePath.c_str(), m_videoWidth, m_videoHeight);

        if (!ReallocateVideoTexture(m_videoWidth, m_videoHeight)) {
            LOG_ERROR("LoadVideo: ReallocateVideoTexture failed.");
            return false;
        }

        m_playbackTimeMs = 0.0;
        m_currentFrameTimestamp = -1.0;
        m_playbackTimer.Reset();

        m_pActiveSRV_Y = m_pVideoSRV_Y;
        m_pActiveSRV_UV = m_pVideoSRV_UV;

        m_runThread = true;
        m_videoLoaded = true;
        m_decodeThread = std::thread(&VideoDecoder::DecodingThreadProc, this);

        LOG_INFO("LoadVideo: Decoding thread successfully spawned.");
        return true;
    }
    ```

  - **Inside `UpdateFrame` (line 387)**:
    Log path selections (Hardware vs Software) and validate decoded frames:
    ```cpp
    bool VideoDecoder::UpdateFrame(ID3D11DeviceContext* pContext, double& outWaitTimeMs) {
        outWaitTimeMs = 0.0;
        if (!m_videoLoaded || !m_pVideoTexture) return false;

        double elapsed = m_playbackTimer.GetElapsedMilliseconds();
        if (m_isPaused.load()) return false;

        if (m_currentFrameTimestamp >= 0.0) {
            m_playbackTimeMs = m_currentFrameTimestamp + elapsed;
        } else {
            m_playbackTimeMs += elapsed;
            m_playbackTimer.Reset();
        }

        Microsoft::WRL::ComPtr<IMFSample> pSelectedSample;
        bool hasNewFrame = false;

        while (true) {
            IMFSample* frontSample = m_sampleQueue.Peek();
            if (!frontSample) {
                break;
            }

            LONGLONG hnsTimestamp = 0;
            HRESULT hrTime = frontSample->GetSampleTime(&hnsTimestamp);
            if (FAILED(hrTime)) {
                LOG_WARN("UpdateFrame: GetSampleTime failed on sample (HRESULT: 0x%08X). Discarding sample.", hrTime);
                m_sampleQueue.PopAndDiscard();
                continue;
            }

            double sampleTimeMs = static_cast<double>(hnsTimestamp) / 10000.0;

            if (m_currentFrameTimestamp < 0.0) {
                LOG_INFO("UpdateFrame: First sample identified. sampleTimeMs = %.2f ms", sampleTimeMs);
                m_playbackTimeMs = sampleTimeMs;
                m_currentFrameTimestamp = sampleTimeMs;
                IMFSample* poppedSample = nullptr;
                if (m_sampleQueue.Pop(poppedSample)) {
                    pSelectedSample.Attach(poppedSample);
                    hasNewFrame = true;
                }
                continue;
            }

            if (sampleTimeMs < m_currentFrameTimestamp) {
                LOG_INFO("UpdateFrame: Video loop detected. Resetting playback timeline. new sampleTimeMs = %.2f ms, previous = %.2f ms", sampleTimeMs, m_currentFrameTimestamp);
                m_playbackTimeMs = sampleTimeMs;
                m_currentFrameTimestamp = sampleTimeMs;
                IMFSample* poppedSample = nullptr;
                if (m_sampleQueue.Pop(poppedSample)) {
                    pSelectedSample.Attach(poppedSample);
                    hasNewFrame = true;
                }
                continue;
            }

            if (m_playbackTimeMs >= sampleTimeMs) {
                m_currentFrameTimestamp = sampleTimeMs;
                IMFSample* poppedSample = nullptr;
                if (m_sampleQueue.Pop(poppedSample)) {
                    pSelectedSample.Attach(poppedSample);
                    hasNewFrame = true;
                }
            } else {
                outWaitTimeMs = sampleTimeMs - m_playbackTimeMs;
                break;
            }
        }

        if (!hasNewFrame && m_sampleQueue.IsEmpty()) {
            outWaitTimeMs = 2.0;
        }

        if (!hasNewFrame || !pSelectedSample) {
            return false;
        }

        m_playbackTimer.Reset();

        Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
        HRESULT hr = pSelectedSample->GetBufferByIndex(0, &pBuffer);
        if (FAILED(hr)) {
            LOG_ERROR("UpdateFrame: GetBufferByIndex failed. HRESULT = 0x%08X", hr);
            return false;
        }

        // --- Hardware Path (DXGI GPU-to-GPU Copy) ---
        Microsoft::WRL::ComPtr<IMFDXGIBuffer> pDXGIBuffer;
        hr = pBuffer.As(&pDXGIBuffer);
        if (SUCCEEDED(hr)) {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> pMFTexture;
            hr = pDXGIBuffer->GetResource(IID_PPV_ARGS(&pMFTexture));
            if (SUCCEEDED(hr)) {
                D3D11_TEXTURE2D_DESC mfDesc;
                pMFTexture->GetDesc(&mfDesc);
                
                LOG_DEBUG("UpdateFrame: Hardware DXGI Path Selected. Decoded size = %dx%d, Local texture size = %dx%d",
                    mfDesc.Width, mfDesc.Height, m_videoTextureWidth, m_videoTextureHeight);

                if (mfDesc.Width != m_videoTextureWidth || mfDesc.Height != m_videoTextureHeight) {
                    LOG_INFO("UpdateFrame: Reallocating local texture to match hardware size %dx%d", mfDesc.Width, mfDesc.Height);
                    if (!ReallocateVideoTexture(mfDesc.Width, mfDesc.Height)) {
                        return false;
                    }
                }

                UINT subresourceIndex = 0;
                pDXGIBuffer->GetSubresourceIndex(&subresourceIndex);

                pContext->CopySubresourceRegion(
                    m_pVideoTexture.Get(),
                    0, 0, 0, 0,
                    pMFTexture.Get(),
                    subresourceIndex,
                    nullptr
                );

                m_pActiveSRV_Y = m_pVideoSRV_Y;
                m_pActiveSRV_UV = m_pVideoSRV_UV;
                return true;
            }
        }

        // --- Software Path (2D System Buffer Copy) ---
        Microsoft::WRL::ComPtr<IMF2DBuffer> p2DBuffer;
        hr = pBuffer.As(&p2DBuffer);
        if (SUCCEEDED(hr)) {
            LOG_DEBUG("UpdateFrame: Software 2D Buffer Path Selected. Size = %dx%d", m_videoWidth, m_videoHeight);
            if (m_videoWidth != m_videoTextureWidth || m_videoHeight != m_videoTextureHeight) {
                LOG_INFO("UpdateFrame: Reallocating local texture to match software size %dx%d", m_videoWidth, m_videoHeight);
                if (!ReallocateVideoTexture(m_videoWidth, m_videoHeight)) {
                    return false;
                }
            }
            BYTE* pScanline0 = nullptr;
            LONG pitch = 0;
            hr = p2DBuffer->Lock2D(&pScanline0, &pitch);
            if (SUCCEEDED(hr)) {
                pContext->UpdateSubresource(
                    m_pVideoTexture.Get(),
                    0,
                    nullptr,
                    pScanline0,
                    pitch,
                    0
                );
                p2DBuffer->Unlock2D();
                m_pActiveSRV_Y = m_pVideoSRV_Y;
                m_pActiveSRV_UV = m_pVideoSRV_UV;
                return true;
            }
        }

        // --- Secondary Software Path (Contiguous Buffer Copy) ---
        BYTE* pData = nullptr;
        DWORD cbCurrentLength = 0;
        hr = pBuffer->Lock(&pData, nullptr, &cbCurrentLength);
        if (SUCCEEDED(hr)) {
            LOG_DEBUG("UpdateFrame: Contiguous Buffer Software Path Selected. Size = %dx%d, Length = %u", m_videoWidth, m_videoHeight, cbCurrentLength);
            if (m_videoWidth != m_videoTextureWidth || m_videoHeight != m_videoTextureHeight) {
                LOG_INFO("UpdateFrame: Reallocating local texture to match software size %dx%d", m_videoWidth, m_videoHeight);
                if (!ReallocateVideoTexture(m_videoWidth, m_videoHeight)) {
                    pBuffer->Unlock();
                    return false;
                }
            }
            UINT32 rowPitch = m_videoWidth;
            pContext->UpdateSubresource(
                m_pVideoTexture.Get(),
                0,
                nullptr,
                pData,
                rowPitch,
                0
            );
            pBuffer->Unlock();
            m_pActiveSRV_Y = m_pVideoSRV_Y;
            m_pActiveSRV_UV = m_pVideoSRV_UV;
            return true;
        }

        LOG_ERROR("UpdateFrame: All frame extraction paths failed to extract media buffer.");
        return false;
    }
    ```

---

### File 7: `src/video_renderer.cpp`
- **Purpose**: Instrument Shader Resource View (SRV) binding, viewport configurations, and the drawing call.
- **Proposed Changes in `RenderVideoFrame` (line 206)**:
  ```cpp
  HRESULT VideoRenderer::RenderVideoFrame(
      ID3D11ShaderResourceView* pVideoSRV_Y, 
      ID3D11ShaderResourceView* pVideoSRV_UV, 
      int textureWidth, 
      int textureHeight, 
      int videoWidth, 
      int videoHeight
  ) {
      if (!m_pDeviceManager || !m_pSwapChainManager) {
          LOG_ERROR("RenderVideoFrame: DeviceManager or SwapChainManager reference is null.");
          return E_FAIL;
      }
      auto d3dContext = m_pDeviceManager->GetContext();
      auto rtv = m_pSwapChainManager->GetRenderTargetView();

      if (!d3dContext || !rtv) {
          LOG_ERROR("RenderVideoFrame: D3D11 context (%p) or RenderTargetView (%p) is null.", d3dContext, rtv);
          return E_FAIL;
      }
      if (!pVideoSRV_Y || !pVideoSRV_UV) {
          LOG_ERROR("RenderVideoFrame: Y SRV (%p) or UV SRV (%p) is null.", pVideoSRV_Y, pVideoSRV_UV);
          return E_FAIL;
      }

      D3D11_VIEWPORT vp = { 0 };
      vp.Width = (float)m_pSwapChainManager->GetWidth();
      vp.Height = (float)m_pSwapChainManager->GetHeight();
      vp.MinDepth = 0.0f;
      vp.MaxDepth = 1.0f;
      
      LOG_DEBUG("RenderVideoFrame: Setting Viewport: Width = %.1f, Height = %.1f", vp.Width, vp.Height);
      d3dContext->RSSetViewports(1, &vp);
      d3dContext->OMSetRenderTargets(1, &rtv, NULL);

      UpdateAspectRatioCB(textureWidth, textureHeight, videoWidth, videoHeight);

      d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
      d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
      d3dContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
      
      ID3D11ShaderResourceView* srvs[2] = { pVideoSRV_Y, pVideoSRV_UV };
      LOG_DEBUG("RenderVideoFrame: Binding SRVs: Y = %p, UV = %p", pVideoSRV_Y, pVideoSRV_UV);
      d3dContext->PSSetShaderResources(0, 2, srvs);
      d3dContext->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

      d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      d3dContext->IASetInputLayout(nullptr);

      LOG_DEBUG("RenderVideoFrame: Executing draw call...");
      d3dContext->Draw(3, 0);

      ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
      d3dContext->PSSetShaderResources(0, 2, nullSRVs);

      return S_OK;
  }
  ```

---

## 3. Side-by-Side Diagnostic Comparison Model

This section illustrates how the structured logs look in a working scenario vs. common failure points.

### Case A: Working Run (Dev Machine)
```
[INFO] WinMain starting. CmdLine: ''
[INFO] WinMain: Initializing COM library (COINIT_APARTMENTTHREADED)...
[INFO] WinMain: CoInitializeEx completed. HRESULT = 0x00000000
[INFO] ExplorerIntegration::Initialize entry. hInstance = 00007FF7B10D0000
[INFO] ExplorerIntegration::FindWorkerW entry.
[INFO] FindWorkerW: FindWindowW('Progman') result = 00000000000100AC
[INFO] FindWorkerW: Sending 0x052C to Progman...
[INFO] FindWorkerW: SendMessageTimeoutW(0x052C) returned 1, result = 0x0000000000000000
[INFO] FindWorkerW: SHELLDLL_DefView found in WorkerW parent: 0000000000020054
[INFO] FindWorkerW: Found empty WorkerW for wallpaper injection: 00000000000300A8
[INFO] FindWorkerW: Dedicated wallpaper WorkerW assigned: 00000000000300A8
[INFO] ExplorerIntegration::CreateHostWindow entry.
[INFO] CreateHostWindow: Registered window class 'LiveWallpaperHostClass'.
[INFO] CreateHostWindow: Virtual screen metrics: x=0, y=0, width=1920, height=1080
[INFO] CreateHostWindow: CreateWindowExW returned HWND = 00000000000401C0 (Parent: 00000000000300A8). Error: 0
[INFO] ExplorerIntegration::InjectIntoDesktop entry.
[INFO] InjectIntoDesktop: Current parent is 00000000000300A8 (Expected: 00000000000300A8)
[INFO] InjectIntoDesktop: Arranging host window position: After = 0000000000000001, x=0, y=0, cx=1920, cy=1080
[INFO] InjectIntoDesktop: SetWindowPos completed. Result = 1
[INFO] InjectIntoDesktop: Final injected state verification: HWND = 00000000000401C0, Parent = 00000000000300A8 (Expected: 00000000000300A8)
[INFO] Explorer Integration successfully initialized and injected.
[INFO] RenderThreadController::ThreadProc entry.
[INFO] ThreadProc: Initializing multi-threaded COM library (COINIT_MULTITHREADED)...
[INFO] ThreadProc: CoInitializeEx completed. HRESULT = 0x00000000
[INFO] DeviceManager::Initialize entry.
[INFO] DeviceManager::Initialize: Attempting Hardware D3D11 device creation. Flags = 0x00000022
[INFO] DeviceManager::Initialize: Hardware device creation outcome: HRESULT = 0x00000000, Supported Feature Level = 0x0000B001
[INFO] DeviceManager::Initialize: Active Adapter: NVIDIA GeForce RTX 3080 (VendorID: 0x10DE, DeviceID: 0x2206, VideoMemory: 10240 MB)
[INFO] DeviceManager::Initialize: QueryInterface ID3D10Multithread result = 0x00000000
[INFO] DeviceManager::Initialize: Context multithreading protection enabled.
[INFO] DeviceManager successfully initialized.
[INFO] SwapChainManager::Initialize entry. HWND = 00000000000401C0, width = 1920, height = 1080
[INFO] SwapChainManager::CreateSwapChain entry.
[INFO] CreateSwapChain: QueryInterface IDXGIDevice result = 0x00000000
[INFO] CreateSwapChain: GetAdapter result = 0x00000000
[INFO] CreateSwapChain: GetParent IDXGIFactory2 result = 0x00000000
[INFO] CreateSwapChain: Calling CreateSwapChainForHwnd. HWND = 00000000000401C0, Dimensions = 1920x1080
[INFO] CreateSwapChain: CreateSwapChainForHwnd result = 0x00000000
[INFO] SwapChainManager::CreateRenderTargetView entry.
[INFO] CreateRenderTargetView: GetBuffer(0) result = 0x00000000
[INFO] CreateRenderTargetView: CreateRenderTargetView result = 0x00000000
[INFO] SwapChainManager successfully initialized (1920 x 1080).
[INFO] VideoDecoder::Initialize entry.
[INFO] VideoDecoder::Initialize: QueryInterface ID3D10Multithread result = 0x00000000
[INFO] VideoDecoder::Initialize: Context multithreading protection enabled.
[INFO] VideoDecoder::Initialize: MFStartup result = 0x00000000
[INFO] VideoDecoder::Initialize: MFCreateDXGIDeviceManager result = 0x00000000, ResetToken = 1
[INFO] VideoDecoder::Initialize: IMFDXGIDeviceManager::ResetDevice result = 0x00000000
[INFO] VideoDecoder initialized successfully with DXVA2/D3D11 hardware acceleration.
[INFO] VideoDecoder::LoadVideo entry. FilePath = C:\Users\Dev\Videos\wallpaper.mp4
[INFO] LoadVideo: Attempting fallback combination 1/4: 'D3D Manager + Video Processing'
[INFO] LoadVideo: MFCreateAttributes result = 0x00000000
[INFO] LoadVideo: MFCreateSourceReaderFromURL result = 0x00000000
[INFO] LoadVideo: SetCurrentMediaType(NV12) result = 0x00000000
[INFO] LoadVideo: Success using combination 'D3D Manager + Video Processing'
[INFO] LoadVideo: GetCurrentMediaType result = 0x00000000
[INFO] LoadVideo: MFGetAttributeSize result = 0x00000000. Width = 1920, Height = 1080
[INFO] LoadVideo: Codec subtype matched: H.264 / AVC
[INFO] LoadVideo: Loaded video dimensions: C:\Users\Dev\Videos\wallpaper.mp4 (1920x1080)
[INFO] Reallocated local video texture to match hardware/software frame size: 1920x1080
[INFO] VideoDecoder::LoadVideo: Decoding thread successfully spawned.
[INFO] VideoDecoder background thread started.
[INFO] UpdateFrame: First sample identified. sampleTimeMs = 0.00 ms
[DEBUG] UpdateFrame: Hardware DXGI Path Selected. Decoded size = 1920x1080, Local texture size = 1920x1080
[DEBUG] RenderVideoFrame: Setting Viewport: Width = 1920.0, Height = 1080.0
[DEBUG] RenderVideoFrame: Binding SRVs: Y = 000000000005AB00, UV = 000000000005AC10
[DEBUG] RenderVideoFrame: Executing draw call...
[DEBUG] SwapChainManager::Present succeeded. HRESULT = 0x00000000
[INFO] MILESTONE: First video frame successfully decoded AND presented to desktop. TimeTick: 4810052
```

---

### Case B: Failing Run (Target Machine)
A comparison reveals exactly where execution diverged. For example, if a target machine has corrupt media codecs or missing GPU hardware rendering capabilities:

```
... [Standard window and Explorer integration success logs] ...
[INFO] DeviceManager::Initialize: Attempting Hardware D3D11 device creation. Flags = 0x00000022
[WARN] Hardware D3D11 Device creation failed (HRESULT: 0x887A0004). Falling back to WARP driver...
[INFO] DeviceManager::Initialize: WARP device creation outcome: HRESULT = 0x00000000, Supported Feature Level = 0x0000B001
[INFO] DeviceManager::Initialize: Active Adapter: Microsoft Basic Render Driver (VendorID: 0x1414, DeviceID: 0x008C, VideoMemory: 0 MB)
...
[INFO] VideoDecoder::Initialize entry.
[INFO] VideoDecoder::Initialize: MFStartup result = 0x00000000
[INFO] VideoDecoder::Initialize: MFCreateDXGIDeviceManager result = 0x00000000, ResetToken = 1
[INFO] VideoDecoder::Initialize: IMFDXGIDeviceManager::ResetDevice result = 0x00000000
[INFO] VideoDecoder initialized successfully with DXVA2/D3D11 hardware acceleration.
[INFO] VideoDecoder::LoadVideo entry. FilePath = C:\Users\Target\Videos\wallpaper.mp4
[INFO] LoadVideo: Attempting fallback combination 1/4: 'D3D Manager + Video Processing'
[INFO] LoadVideo: MFCreateAttributes result = 0x00000000
[INFO] LoadVideo: MFCreateSourceReaderFromURL result = 0xC00D5212
[WARN] LoadVideo: MFCreateSourceReaderFromURL failed for 'D3D Manager + Video Processing'. HRESULT: 0xC00D5212
[INFO] LoadVideo: Attempting fallback combination 2/4: 'D3D Manager Only'
[INFO] LoadVideo: MFCreateSourceReaderFromURL result = 0xC00D5212
[WARN] LoadVideo: MFCreateSourceReaderFromURL failed for 'D3D Manager Only'. HRESULT: 0xC00D5212
[INFO] LoadVideo: Attempting fallback combination 3/4: 'Software + Video Processing'
[INFO] LoadVideo: MFCreateAttributes result = 0x00000000
[INFO] LoadVideo: MFCreateSourceReaderFromURL result = 0xC00D5212
[WARN] LoadVideo: MFCreateSourceReaderFromURL failed for 'Software + Video Processing'. HRESULT: 0xC00D5212
[INFO] LoadVideo: Attempting fallback combination 4/4: 'Software (No Attributes)'
[INFO] LoadVideo: MFCreateSourceReaderFromURL result = 0xC00D5212
[WARN] LoadVideo: MFCreateSourceReaderFromURL failed for 'Software (No Attributes)'. HRESULT: 0xC00D5212
[ERROR] LoadVideo: All Source Reader creation attempts failed for path: C:\Users\Target\Videos\wallpaper.mp4
```

**Divergence Analysis**:
- The log immediately shows that the target machine was forced to fall back to `WARP` (software emulation adapter `Microsoft Basic Render Driver`) due to a failed hardware device creation with code `0x887A0004` (DXGI_ERROR_UNSUPPORTED).
- It also shows that the loading failed in Media Foundation with HRESULT `0xC00D5212` (`MF_E_TOPO_CODEC_NOT_FOUND`), demonstrating that the system lacks the required decoder codec for the video format.
