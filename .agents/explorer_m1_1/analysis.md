# Render Pipeline Diagnostic Instrumentation Analysis (Requirement R1)

## Executive Summary
This report analyzes the changes required to implement **Requirement R1 (Render Pipeline Diagnostic Instrumentation)** in the Windows Live Wallpaper Engine project. 
To diagnose issues on target user machines where rendering might fail, we must add comprehensive logging throughout the render pipeline—from application startup to the message loop, D3D11 device creation, Media Foundation initialization, video decoding, frame updates, rendering, and swap chain presentation.

The proposed modifications utilize the existing `Utils::Log` / `Utils::LogW` logging framework (using `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, `LOG_DEBUG` macros). To prevent performance degradation and huge log file growth on target machines, high-frequency execution stages (such as `UpdateFrame`, `RenderVideoFrame`, and `Present`) will use `LOG_DEBUG` (which is compiled out in Release mode) or be throttled/logged selectively, while critical milestones (such as the first decoded and presented frame) will use `LOG_INFO` to ensure visibility.

---

## Affected Files Map
The following files require modification:
| # | File Path | Scope of Change |
|---|---|---|
| 1 | `src/main.cpp` | Instrument WinMain entry and COM initialization (`CoInitializeEx`). |
| 2 | `src/explorer_integration.cpp` | Instrument WorkerW discovery, host window creation, and desktop injection. |
| 3 | `src/render_thread_controller.h` | Add state tracking for the first frame milestone (`m_firstFrameMilestoneLogged`). |
| 4 | `src/render_thread_controller.cpp` | Instrument render thread COM setup, lifecycle milestones, and first-frame detection. |
| 5 | `src/device_manager.cpp` | Instrument D3D11 device/context creation (Hardware and WARP) and query DXGI Adapter info. |
| 6 | `src/swap_chain_manager.cpp` | Instrument swap chain creation, RTV setup, buffer resizing, and present HRESULT logging. |
| 7 | `src/video_decoder.cpp` | Instrument MF startup, DXGI device manager, reader fallbacks, and update-frame paths. |
| 8 | `src/video_renderer.cpp` | Instrument SRV binding, viewport configuration, and draw call execution. |

---

## Detailed Instrumentation Breakdown

### Stage 1: Application Start & COM Initialization
* **File**: `src/main.cpp`
* **Target Line**: ~71 (inside `WinMain`)
* **Logic**: Log when the application starts, log entry to COM initialization, and log success/failure of `CoInitializeEx` with its HRESULT.
* **Proposed Code Change**:
  ```cpp
  // Replace lines 71-74:
  LOG_INFO("WinMain: Initializing COM library via CoInitializeEx (Apartment-threaded)...");
  HRESULT hrCOM = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  if (SUCCEEDED(hrCOM)) {
      LOG_INFO("WinMain: CoInitializeEx succeeded. HRESULT = 0x%08X", hrCOM);
  } else {
      LOG_ERROR("WinMain: CoInitializeEx failed in main thread. HRESULT: 0x%08X", hrCOM);
  }
  ```

---

### Stage 2: Explorer Integration (WorkerW, Host Window, Injection)
* **File**: `src/explorer_integration.cpp`
* **Target Lines**:
  * `Initialize` (~line 10)
  * `FindWorkerW` (~line 50)
  * `CreateHostWindow` (~line 124)
  * `InjectIntoDesktop` (~line 153)
* **Logic**: Track every step of discovery and injection with handles, window sizes, and Win32 errors.
* **Proposed Code Changes**:
  * **In `Initialize`**:
    ```cpp
    bool ExplorerIntegration::Initialize(HINSTANCE hInstance) {
        m_hInstance = hInstance;
        m_isShuttingDown.store(false);
        LOG_INFO("ExplorerIntegration::Initialize: Entry. hInstance = 0x%p", hInstance);

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
        LOG_INFO("ExplorerIntegration::Initialize: Explorer Integration successfully initialized and injected.");
        return true;
    }
    ```
  * **In `FindWorkerW`**:
    ```cpp
    bool ExplorerIntegration::FindWorkerW() {
        LOG_INFO("ExplorerIntegration::FindWorkerW: Entry.");
        HWND progman = FindWindowW(L"Progman", NULL);
        if (!progman) {
            LOG_ERROR("ExplorerIntegration::FindWorkerW: Progman window not found.");
            return false;
        }
        LOG_INFO("ExplorerIntegration::FindWorkerW: Found Progman = 0x%p", progman);

        LOG_INFO("ExplorerIntegration::FindWorkerW: Sending 0x052C to Progman. Timestamp: %llu", GetTickCount64());
        ULONG_PTR result = 0;
        LRESULT lr = SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_ABORTIFHUNG, 1000, &result);
        LOG_INFO("ExplorerIntegration::FindWorkerW: SendMessageTimeoutW returned LRESULT = %ld, result = %lu, GetLastError = %u", lr, result, GetLastError());

        HWND shellDefView = NULL;
        HWND parentOfShell = NULL;
        HWND wallpaperWorkerW = NULL;

        shellDefView = FindWindowExW(progman, NULL, L"SHELLDLL_DefView", NULL);
        LOG_INFO("ExplorerIntegration::FindWorkerW: Initial SHELLDLL_DefView search in Progman returned 0x%p", shellDefView);
        if (shellDefView) {
            parentOfShell = progman;
        } else {
            HWND workerW = FindWindowExW(NULL, NULL, L"WorkerW", NULL);
            while (workerW) {
                LOG_INFO("ExplorerIntegration::FindWorkerW: Enumerating WorkerW = 0x%p", workerW);
                shellDefView = FindWindowExW(workerW, NULL, L"SHELLDLL_DefView", NULL);
                if (shellDefView) {
                    parentOfShell = workerW;
                    LOG_INFO("ExplorerIntegration::FindWorkerW: Found SHELLDLL_DefView = 0x%p inside WorkerW = 0x%p", shellDefView, workerW);
                    break;
                }
                workerW = FindWindowExW(NULL, workerW, L"WorkerW", NULL);
            }
        }

        if (!shellDefView) {
            LOG_ERROR("ExplorerIntegration::FindWorkerW: SHELLDLL_DefView not found anywhere.");
            return false;
        }

        if (parentOfShell == progman) {
            m_hWorkerW = progman;
            m_hShellDefView = shellDefView;
            m_useLegacyWorkerW = false;
            LOG_WARN("ExplorerIntegration::FindWorkerW: Fallback to Progman triggered. Target HWND = 0x%p", m_hWorkerW);
        } else {
            HWND workerW = FindWindowExW(NULL, NULL, L"WorkerW", NULL);
            while (workerW) {
                LOG_INFO("ExplorerIntegration::FindWorkerW: Enumerating WorkerW (Pass 2) = 0x%p", workerW);
                if (workerW != parentOfShell && !FindWindowExW(workerW, NULL, L"SHELLDLL_DefView", NULL)) {
                    wallpaperWorkerW = workerW;
                    LOG_INFO("ExplorerIntegration::FindWorkerW: Found empty WorkerW for wallpaper = 0x%p", workerW);
                    break;
                }
                workerW = FindWindowExW(NULL, workerW, L"WorkerW", NULL);
            }

            if (wallpaperWorkerW) {
                m_hWorkerW = wallpaperWorkerW;
                m_hShellDefView = NULL;
                m_useLegacyWorkerW = true;
                LOG_INFO("ExplorerIntegration::FindWorkerW: Successfully assigned dedicated WorkerW = 0x%p", m_hWorkerW);
            } else {
                m_hWorkerW = progman;
                m_hShellDefView = NULL;
                m_useLegacyWorkerW = false;
                LOG_WARN("ExplorerIntegration::FindWorkerW: Fallback to Progman triggered (no empty WorkerW found). Target HWND = 0x%p", m_hWorkerW);
            }
        }

        LOG_INFO("ExplorerIntegration::FindWorkerW: Complete. m_hWorkerW = 0x%p, m_hShellDefView = 0x%p", m_hWorkerW, m_hShellDefView);
        return m_hWorkerW != nullptr;
    }
    ```
  * **In `CreateHostWindow`**:
    ```cpp
    bool ExplorerIntegration::CreateHostWindow(HINSTANCE hInstance) {
        LOG_INFO("ExplorerIntegration::CreateHostWindow: Entry.");
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
                LOG_ERROR("ExplorerIntegration::CreateHostWindow: Failed to register window class. GetLastError = %u", GetLastError());
                return false;
            }
            LOG_INFO("ExplorerIntegration::CreateHostWindow: Registered window class 'LiveWallpaperHostClass'.");
        }

        int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        LOG_INFO("ExplorerIntegration::CreateHostWindow: Virtual screen metrics: Origin=(%d, %d), Size=%dx%d", x, y, cx, cy);

        LOG_INFO("ExplorerIntegration::CreateHostWindow: Calling CreateWindowExW. Parent WorkerW = 0x%p", m_hWorkerW);
        m_hWnd = CreateWindowExW(
            WS_EX_NOACTIVATE, L"LiveWallpaperHostClass", L"LiveWallpaperHost",
            WS_CHILD | WS_VISIBLE, x, y, cx, cy, m_hWorkerW, NULL, hInstance, this
        );

        if (m_hWnd) {
            LOG_INFO("ExplorerIntegration::CreateHostWindow: CreateWindowExW succeeded. m_hWnd = 0x%p", m_hWnd);
        } else {
            LOG_ERROR("ExplorerIntegration::CreateHostWindow: CreateWindowExW failed. GetLastError = %u", GetLastError());
        }

        return m_hWnd != nullptr;
    }
    ```
  * **In `InjectIntoDesktop`**:
    ```cpp
    bool ExplorerIntegration::InjectIntoDesktop() {
        LOG_INFO("ExplorerIntegration::InjectIntoDesktop: Entry. m_hWnd = 0x%p, m_hWorkerW = 0x%p", m_hWnd, m_hWorkerW);
        if (!m_hWnd || !m_hWorkerW) return false;

        HWND currentParent = GetParent(m_hWnd);
        LOG_INFO("ExplorerIntegration::InjectIntoDesktop: Current parent HWND = 0x%p", currentParent);
        if (currentParent != m_hWorkerW) {
            HWND prevParent = SetParent(m_hWnd, m_hWorkerW);
            LOG_INFO("ExplorerIntegration::InjectIntoDesktop: SetParent completed. Previous parent = 0x%p, New parent verified = 0x%p", prevParent, GetParent(m_hWnd));
        }

        int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        HWND hWndInsertAfter = HWND_BOTTOM;
        if (!m_useLegacyWorkerW && m_hShellDefView) {
            hWndInsertAfter = m_hShellDefView;
        }

        LOG_INFO("ExplorerIntegration::InjectIntoDesktop: Positioning window (InsertAfter = 0x%p, Coordinates=(%d,%d,%d,%d))...", hWndInsertAfter, x, y, cx, cy);
        if (SetWindowPos(m_hWnd, hWndInsertAfter, x, y, cx, cy, SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
            LOG_INFO("ExplorerIntegration::InjectIntoDesktop: SetWindowPos succeeded.");
        } else {
            LOG_WARN("ExplorerIntegration::InjectIntoDesktop: SetWindowPos failed. GetLastError = %u", GetLastError());
        }

        HWND finalParent = GetParent(m_hWnd);
        LOG_INFO("ExplorerIntegration::InjectIntoDesktop: Complete. Final parent = 0x%p, Expected = 0x%p", finalParent, m_hWorkerW);
        return true;
    }
    ```

---

### Stage 3: Render Thread & Subsystem Initialization
* **Files**: `src/render_thread_controller.h`, `src/render_thread_controller.cpp`, `src/device_manager.cpp`, `src/swap_chain_manager.cpp`
* **Target Lines**:
  * `RenderThreadController::ThreadProc` (~line 82)
  * `DeviceManager::Initialize` (~line 10)
  * `SwapChainManager::Initialize` (~line 10)
* **Logic**:
  * Log COM thread initialization entry and HRESULT.
  * Log step-by-step creation of `DeviceManager` and `SwapChainManager`.
  * Retrieve DXGI Adapter description, vendor/device IDs, and dedicated VRAM.
  * Log the chosen D3D feature level.
  * Log swap chain descriptor fields and RTV creation HRESULTs.
* **Proposed Code Changes**:
  * **In `RenderThreadController::ThreadProc`**:
    ```cpp
    void RenderThreadController::ThreadProc() {
        LOG_INFO("RenderThreadController::ThreadProc: Thread started.");
        m_screenCleared = false;
        m_firstFrameMilestoneLogged = false; // Reset milestone flag

        LOG_INFO("RenderThreadController::ThreadProc: Initializing COM library (CoInitializeEx)...");
        HRESULT hrCOM = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hrCOM)) {
            LOG_INFO("RenderThreadController::ThreadProc: CoInitializeEx succeeded. HRESULT = 0x%08X", hrCOM);
        } else {
            LOG_ERROR("RenderThreadController::ThreadProc: CoInitializeEx failed. HRESULT: 0x%08X", hrCOM);
        }

        m_deviceManager = std::make_unique<DeviceManager>();
        m_swapChainManager = std::make_unique<SwapChainManager>();
        m_videoRenderer = std::make_unique<VideoRenderer>();
        m_decoder = std::make_unique<VideoDecoder>();
        m_shaderBridge = std::make_unique<FFIShaderBridge>();

        if (m_hWnd && !m_videoPath.empty()) {
            RECT rect;
            GetClientRect(m_hWnd, &rect);
            int w = rect.right - rect.left;
            int h = rect.bottom - rect.top;
            if (w == 0) w = 800;
            if (h == 0) h = 600;

            LOG_INFO("RenderThreadController::ThreadProc: Initializing D3D11 subsystems (Width = %d, Height = %d)...", w, h);

            LOG_INFO("RenderThreadController::ThreadProc: Initializing DeviceManager...");
            if (m_deviceManager->Initialize()) {
                LOG_INFO("RenderThreadController::ThreadProc: DeviceManager initialized. Initializing SwapChainManager...");
                if (m_swapChainManager->Initialize(m_deviceManager->GetDevice(), m_hWnd, w, h)) {
                    LOG_INFO("RenderThreadController::ThreadProc: SwapChainManager initialized.");
                    m_videoRenderer->Initialize(m_deviceManager.get(), m_swapChainManager.get());
                    
                    if (IsShaderFile(m_videoPath)) {
                        LOG_INFO_W(L"RenderThreadController::ThreadProc: Initializing Shader Host for: %ls", m_videoPath.c_str());
                        if (m_shaderBridge->Load()) {
                            HRESULT hr = m_shaderBridge->InitShaderHost(
                                m_deviceManager->GetDevice(),
                                m_deviceManager->GetContext(),
                                m_videoPath,
                                &m_shaderHost
                            );
                            if (FAILED(hr)) LOG_ERROR("RenderThreadController::ThreadProc: InitShaderHost failed. HR = 0x%08X", hr);
                        }
                    } else {
                        LOG_INFO_W(L"RenderThreadController::ThreadProc: Initializing VideoDecoder for: %ls", m_videoPath.c_str());
                        if (m_decoder->Initialize(m_deviceManager->GetDevice())) {
                            m_decoder->LoadVideo(m_videoPath);
                        } else {
                            LOG_ERROR("RenderThreadController::ThreadProc: VideoDecoder initialization failed.");
                        }
                    }
                } else {
                    LOG_ERROR("RenderThreadController::ThreadProc: SwapChainManager initialization failed.");
                }
            } else {
                LOG_ERROR("RenderThreadController::ThreadProc: DeviceManager initialization failed.");
            }
        }
        ...
    ```
  * **In `DeviceManager::Initialize`**:
    ```cpp
    bool DeviceManager::Initialize() {
        LOG_INFO("DeviceManager::Initialize: Entry.");

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL supportedLevel = (D3D_FEATURE_LEVEL)0;
        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT; 
    #ifdef _DEBUG
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

        LOG_INFO("DeviceManager::Initialize: Calling D3D11CreateDevice (Hardware, Flags = 0x%X)...", creationFlags);
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

        if (FAILED(hr)) {
            LOG_WARN("DeviceManager::Initialize: Hardware D3D11 Device creation failed. HRESULT = 0x%08X. Falling back to WARP...", hr);
            LOG_INFO("DeviceManager::Initialize: Calling D3D11CreateDevice (WARP)...");
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
        }

        if (FAILED(hr)) {
            LOG_ERROR("DeviceManager::Initialize: Failed to create D3D11 Device (Hardware or WARP). HRESULT = 0x%08X", hr);
            return false;
        }

        LOG_INFO("DeviceManager::Initialize: D3D11 Device created successfully. Selected Feature Level = 0x%04X", supportedLevel);

        // Retrieve and log adapter description (Adapter Info)
        Microsoft::WRL::ComPtr<IDXGIDevice> pDXGIDevice;
        if (SUCCEEDED(m_d3dDevice.As(&pDXGIDevice))) {
            Microsoft::WRL::ComPtr<IDXGIAdapter> pAdapter;
            if (SUCCEEDED(pDXGIDevice->GetAdapter(&pAdapter))) {
                DXGI_ADAPTER_DESC desc;
                if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
                    LOG_INFO_W(L"DeviceManager::Initialize: GPU Adapter Name: %ls", desc.Description);
                    LOG_INFO("DeviceManager::Initialize: GPU Adapter Info: VendorId = 0x%04X, DeviceId = 0x%04X, Revision = %u, VRAM = %zu MB",
                        desc.VendorId, desc.DeviceId, desc.Revision, desc.DedicatedVideoMemory / (1024 * 1024));
                }
            }
        }

        // Enable multithread protection
        Microsoft::WRL::ComPtr<ID3D10Multithread> pMultithread;
        HRESULT hrMT = m_d3dDevice.As(&pMultithread);
        LOG_INFO("DeviceManager::Initialize: Query ID3D10Multithread HRESULT = 0x%08X", hrMT);
        if (SUCCEEDED(hrMT)) {
            pMultithread->SetMultithreadProtected(TRUE);
            LOG_INFO("DeviceManager::Initialize: Enabled multithreading protection on D3D11 Device.");
        }

        LOG_INFO("DeviceManager::Initialize: Successfully completed.");
        return true;
    }
    ```
  * **In `SwapChainManager::Initialize`**:
    ```cpp
    bool SwapChainManager::Initialize(ID3D11Device* device, HWND hWnd, int width, int height) {
        m_hWnd = hWnd;
        m_width = width;
        m_height = height;
        LOG_INFO("SwapChainManager::Initialize: Entry. HWND = 0x%p, Dimensions = %dx%d", hWnd, width, height);

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

        LOG_INFO("SwapChainManager::Initialize: Succeeded. SwapChain = 0x%p, RenderTargetView = 0x%p", m_swapChain.Get(), m_renderTargetView.Get());
        return true;
    }
    ```
  * **In `SwapChainManager::CreateSwapChain`**:
    ```cpp
    bool SwapChainManager::CreateSwapChain(ID3D11Device* device) {
        if (!device) {
            LOG_ERROR("SwapChainManager::CreateSwapChain: Received null D3D11 device pointer.");
            return false;
        }

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        LOG_INFO("SwapChainManager::CreateSwapChain: Query Interface IDXGIDevice HRESULT = 0x%08X", hr);
        if (FAILED(hr)) return false;

        Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
        LOG_INFO("SwapChainManager::CreateSwapChain: GetAdapter HRESULT = 0x%08X", hr);
        if (FAILED(hr)) return false;

        Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
        hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
        LOG_INFO("SwapChainManager::CreateSwapChain: Get IDXGIFactory2 HRESULT = 0x%08X", hr);
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

        LOG_INFO("SwapChainManager::CreateSwapChain: Calling CreateSwapChainForHwnd. HWND = 0x%p, Width = %u, Height = %u, Format = %d, SwapEffect = %d", 
            m_hWnd, scd.Width, scd.Height, scd.Format, scd.SwapEffect);
        hr = dxgiFactory->CreateSwapChainForHwnd(
            device,
            m_hWnd,
            &scd,
            &fsd,
            NULL,
            &m_swapChain
        );
        LOG_INFO("SwapChainManager::CreateSwapChain: CreateSwapChainForHwnd HRESULT = 0x%08X", hr);

        return SUCCEEDED(hr);
    }
    ```
  * **In `SwapChainManager::CreateRenderTargetView`**:
    ```cpp
    bool SwapChainManager::CreateRenderTargetView(ID3D11Device* device) {
        if (!device || !m_swapChain) {
            LOG_ERROR("SwapChainManager::CreateRenderTargetView: Device (0x%p) or SwapChain (0x%p) is null.", device, m_swapChain.Get());
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        LOG_INFO("SwapChainManager::CreateRenderTargetView: Get back-buffer (0) HRESULT = 0x%08X", hr);
        if (FAILED(hr)) return false;

        hr = device->CreateRenderTargetView(backBuffer.Get(), NULL, &m_renderTargetView);
        LOG_INFO("SwapChainManager::CreateRenderTargetView: CreateRenderTargetView HRESULT = 0x%08X", hr);
        return SUCCEEDED(hr);
    }
    ```

---

### Stage 4: Video Decoder Initialization
* **File**: `src/video_decoder.cpp`
* **Target Line**: ~12 (in `VideoDecoder::Initialize`)
* **Logic**: Log COM parameters, Media Foundation startup call, and DXGI Device Manager configuration.
* **Proposed Code Change**:
  ```cpp
  bool VideoDecoder::Initialize(ID3D11Device* pDevice) {
      LOG_INFO("VideoDecoder::Initialize: Entry. pDevice = 0x%p", pDevice);
      if (!pDevice) {
          LOG_ERROR("VideoDecoder::Initialize: Received null D3D11 device pointer.");
          return false;
      }
      m_pDevice = pDevice;

      Microsoft::WRL::ComPtr<ID3D10Multithread> pMultithread;
      HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pMultithread));
      LOG_INFO("VideoDecoder::Initialize: D3D11 Multithread QueryInterface HRESULT = 0x%08X", hr);
      if (SUCCEEDED(hr)) {
          pMultithread->SetMultithreadProtected(TRUE);
          LOG_INFO("VideoDecoder::Initialize: Multithread protection successfully verified/enabled.");
      } else {
          LOG_WARN("VideoDecoder::Initialize: Failed to get ID3D10Multithread. DXVA2 decoding might be unstable.");
      }

      LOG_INFO("VideoDecoder::Initialize: Calling MFStartup...");
      hr = MFStartup(MF_VERSION);
      LOG_INFO("VideoDecoder::Initialize: MFStartup HRESULT = 0x%08X", hr);
      if (FAILED(hr)) {
          LOG_ERROR("VideoDecoder::Initialize: MFStartup failed.");
          return false;
      }

      LOG_INFO("VideoDecoder::Initialize: Calling MFCreateDXGIDeviceManager...");
      hr = MFCreateDXGIDeviceManager(&m_deviceResetToken, &m_pDeviceManager);
      LOG_INFO("VideoDecoder::Initialize: MFCreateDXGIDeviceManager HRESULT = 0x%08X, ResetToken = %u", hr, m_deviceResetToken);
      if (FAILED(hr)) {
          LOG_ERROR("VideoDecoder::Initialize: MFCreateDXGIDeviceManager failed.");
          MFShutdown();
          return false;
      }

      LOG_INFO("VideoDecoder::Initialize: Resetting DXGI Device Manager with D3D11 Device...");
      hr = m_pDeviceManager->ResetDevice(m_pDevice, m_deviceResetToken);
      LOG_INFO("VideoDecoder::Initialize: ResetDevice HRESULT = 0x%08X", hr);
      if (FAILED(hr)) {
          LOG_ERROR("VideoDecoder::Initialize: IMFDXGIDeviceManager::ResetDevice failed.");
          m_pDeviceManager.Reset();
          MFShutdown();
          return false;
      }

      LOG_INFO("VideoDecoder::Initialize: Successfully completed with DXVA2 hardware acceleration support.");
      return true;
  }
  ```

---

### Stage 5: Video Loading & Source Reader Fallbacks
* **File**: `src/video_decoder.cpp`
* **Target Line**: ~64 (in `VideoDecoder::LoadVideo`)
* **Logic**: Log native video attributes (original codec) and step-by-step failures/successes of the 4 Source Reader creation combinations (hardware + postprocessing, hardware-only, software + postprocessing, software-only).
* **Proposed Code Change**:
  ```cpp
  bool VideoDecoder::LoadVideo(const std::wstring& filePath) {
      CloseVideo();
      m_filePath = filePath;
      LOG_INFO_W(L"VideoDecoder::LoadVideo: Entry. FilePath = %ls", filePath.c_str());

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

      for (const auto& opt : options) {
          LOG_INFO("VideoDecoder::LoadVideo: Attempting fallback combination: %s (D3DManager = %d, VideoProcessing = %d)...",
              opt.name, opt.useD3DManager, opt.useVideoProcessing);

          if (opt.useD3DManager && !m_pDeviceManager) {
              LOG_WARN("VideoDecoder::LoadVideo: Skipping option '%s' because m_pDeviceManager is null.", opt.name);
              continue;
          }

          Microsoft::WRL::ComPtr<IMFAttributes> pAttributes;
          UINT32 attrCount = 0;
          if (opt.useD3DManager) attrCount++;
          if (opt.useVideoProcessing) attrCount++;

          if (attrCount > 0) {
              hr = MFCreateAttributes(&pAttributes, attrCount);
              LOG_INFO("VideoDecoder::LoadVideo: MFCreateAttributes result = 0x%08X", hr);
              if (FAILED(hr)) {
                  LOG_WARN("VideoDecoder::LoadVideo: Skip option '%s' due to attribute creation failure.", opt.name);
                  continue;
              }
              if (opt.useD3DManager) {
                  pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_pDeviceManager.Get());
              }
              if (opt.useVideoProcessing) {
                  pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
              }
          }

          LOG_INFO_W(L"VideoDecoder::LoadVideo: Calling MFCreateSourceReaderFromURL for: %ls", m_filePath.c_str());
          hr = MFCreateSourceReaderFromURL(m_filePath.c_str(), pAttributes.Get(), &m_pSourceReader);
          LOG_INFO("VideoDecoder::LoadVideo: MFCreateSourceReaderFromURL result = 0x%08X", hr);
          if (FAILED(hr)) {
              m_pSourceReader.Reset();
              continue;
          }

          hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
          LOG_INFO("VideoDecoder::LoadVideo: Disable all streams HRESULT = 0x%08X", hr);
          if (FAILED(hr)) {
              m_pSourceReader.Reset();
              continue;
          }

          hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
          LOG_INFO("VideoDecoder::LoadVideo: Enable first video stream HRESULT = 0x%08X", hr);
          if (FAILED(hr)) {
              m_pSourceReader.Reset();
              continue;
          }

          // Native Format / Codec Detection
          Microsoft::WRL::ComPtr<IMFMediaType> pNativeType;
          if (SUCCEEDED(m_pSourceReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pNativeType))) {
              GUID nativeSubtype = { 0 };
              if (SUCCEEDED(pNativeType->GetGUID(MF_MT_SUBTYPE, &nativeSubtype))) {
                  std::wstring codecName = L"Unknown Codec";
                  if (nativeSubtype == MFVideoFormat_H264) codecName = L"H.264 / AVC";
                  else if (nativeSubtype == MFVideoFormat_HEVC) codecName = L"H.265 / HEVC";
                  else if (nativeSubtype == MFVideoFormat_WMV3) codecName = L"WMV3 / VC-1";
                  else if (nativeSubtype == MFVideoFormat_MP4V) codecName = L"MPEG-4 Visual";
                  
                  LOG_INFO_W(L"VideoDecoder::LoadVideo: Native video codec detected: %ls (Subtype GUID: {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X})",
                      codecName.c_str(),
                      nativeSubtype.Data1, nativeSubtype.Data2, nativeSubtype.Data3,
                      nativeSubtype.Data4[0], nativeSubtype.Data4[1], nativeSubtype.Data4[2], nativeSubtype.Data4[3],
                      nativeSubtype.Data4[4], nativeSubtype.Data4[5], nativeSubtype.Data4[6], nativeSubtype.Data4[7]);
              }
          }

          Microsoft::WRL::ComPtr<IMFMediaType> pType;
          hr = MFCreateMediaType(&pType);
          LOG_INFO("VideoDecoder::LoadVideo: MFCreateMediaType HRESULT = 0x%08X", hr);
          if (FAILED(hr)) {
              m_pSourceReader.Reset();
              continue;
          }

          pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
          pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);

          hr = m_pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType.Get());
          LOG_INFO("VideoDecoder::LoadVideo: SetCurrentMediaType to NV12 HRESULT = 0x%08X", hr);
          if (FAILED(hr)) {
              m_pSourceReader.Reset();
              continue;
          }

          LOG_INFO("VideoDecoder::LoadVideo: Successfully created Source Reader with option: %s", opt.name);
          initialized = true;
          break;
      }

      if (!initialized || !m_pSourceReader) {
          LOG_ERROR_W(L"VideoDecoder::LoadVideo: All 4 Source Reader creation attempts failed for: %ls.", m_filePath.c_str());
          return false;
      }

      Microsoft::WRL::ComPtr<IMFMediaType> pCurrentType;
      hr = m_pSourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType);
      LOG_INFO("VideoDecoder::LoadVideo: GetCurrentMediaType for active stream HRESULT = 0x%08X", hr);
      if (FAILED(hr)) return false;

      UINT32 width = 0, height = 0;
      hr = MFGetAttributeSize(pCurrentType.Get(), MF_MT_FRAME_SIZE, &width, &height);
      LOG_INFO("VideoDecoder::LoadVideo: Retrieve frame size HRESULT = 0x%08X (Width = %u, Height = %u)", hr, width, height);
      if (FAILED(hr)) return false;

      m_videoWidth = width;
      m_videoHeight = height;

      LOG_INFO_W(L"VideoDecoder::LoadVideo: Successfully loaded: %ls (%dx%d)", m_filePath.c_str(), m_videoWidth, m_videoHeight);

      if (!ReallocateVideoTexture(m_videoWidth, m_videoHeight)) {
          LOG_ERROR("VideoDecoder::LoadVideo: ReallocateVideoTexture failed.");
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

      return true;
  }
  ```

---

### Stage 6: Frame Extraction (Hardware vs. Software Selection & Validity)
* **File**: `src/video_decoder.cpp`
* **Target Line**: ~387 (in `VideoDecoder::UpdateFrame`)
* **Logic**: Add debugging logs to identify whether frames are being extracted via GPU hardware resources (`IMFDXGIBuffer`) or software CPU copies (`IMF2DBuffer` or basic contiguous lock), checking validity along the way.
* **Proposed Code Change**:
  * Note: Since `UpdateFrame` is called at render loop frequencies (60 FPS), we use `LOG_DEBUG` to avoid release-mode log clutter.
  ```cpp
  bool VideoDecoder::UpdateFrame(ID3D11DeviceContext* pContext, double& outWaitTimeMs) {
      outWaitTimeMs = 0.0;
      if (!m_videoLoaded || !m_pVideoTexture) {
          // Silent return or Debug log
          return false;
      }

      double elapsed = m_playbackTimer.GetElapsedMilliseconds();

      if (m_isPaused.load()) {
          return false;
      }

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
          if (FAILED(frontSample->GetSampleTime(&hnsTimestamp))) {
              LOG_ERROR("VideoDecoder::UpdateFrame: GetSampleTime failed on queued frame.");
              m_sampleQueue.PopAndDiscard();
              continue;
          }

          double sampleTimeMs = static_cast<double>(hnsTimestamp) / 10000.0;

          if (m_currentFrameTimestamp < 0.0) {
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
          LOG_ERROR("VideoDecoder::UpdateFrame: GetBufferByIndex failed. HRESULT = 0x%08X", hr);
          return false;
      }

      // Check 1: Hardware DXVA2 Path
      Microsoft::WRL::ComPtr<IMFDXGIBuffer> pDXGIBuffer;
      hr = pBuffer.As(&pDXGIBuffer);
      if (SUCCEEDED(hr)) {
          Microsoft::WRL::ComPtr<ID3D11Texture2D> pMFTexture;
          hr = pDXGIBuffer->GetResource(IID_PPV_ARGS(&pMFTexture));
          if (SUCCEEDED(hr)) {
              D3D11_TEXTURE2D_DESC mfDesc;
              pMFTexture->GetDesc(&mfDesc);

              LOG_DEBUG("VideoDecoder::UpdateFrame: Path = HARDWARE (DXVA2 CopySubresource). Texture = 0x%p, Dimensions = %dx%d, Format = %d",
                  pMFTexture.Get(), mfDesc.Width, mfDesc.Height, mfDesc.Format);

              if (mfDesc.Width != m_videoTextureWidth || mfDesc.Height != m_videoTextureHeight) {
                  if (!ReallocateVideoTexture(mfDesc.Width, mfDesc.Height)) {
                      LOG_ERROR("VideoDecoder::UpdateFrame: ReallocateVideoTexture failed in hardware path.");
                      return false;
                  }
              }

              UINT subresourceIndex = 0;
              pDXGIBuffer->GetSubresourceIndex(&subresourceIndex);

              pContext->CopySubresourceRegion(
                  m_pVideoTexture.Get(), 0, 0, 0, 0,
                  pMFTexture.Get(), subresourceIndex, nullptr
              );

              m_pActiveSRV_Y = m_pVideoSRV_Y;
              m_pActiveSRV_UV = m_pVideoSRV_UV;
              return true;
          }
      }

      // Check 2: Software Path (IMF2DBuffer)
      Microsoft::WRL::ComPtr<IMF2DBuffer> p2DBuffer;
      hr = pBuffer.As(&p2DBuffer);
      if (SUCCEEDED(hr)) {
          LOG_DEBUG("VideoDecoder::UpdateFrame: Path = SOFTWARE (IMF2DBuffer Lock2D). Dimensions = %dx%d", m_videoWidth, m_videoHeight);

          if (m_videoWidth != m_videoTextureWidth || m_videoHeight != m_videoTextureHeight) {
              if (!ReallocateVideoTexture(m_videoWidth, m_videoHeight)) {
                  LOG_ERROR("VideoDecoder::UpdateFrame: ReallocateVideoTexture failed in software path.");
                  return false;
              }
          }
          BYTE* pScanline0 = nullptr;
          LONG pitch = 0;
          hr = p2DBuffer->Lock2D(&pScanline0, &pitch);
          if (SUCCEEDED(hr)) {
              pContext->UpdateSubresource(m_pVideoTexture.Get(), 0, nullptr, pScanline0, pitch, 0);
              p2DBuffer->Unlock2D();
              m_pActiveSRV_Y = m_pVideoSRV_Y;
              m_pActiveSRV_UV = m_pVideoSRV_UV;
              return true;
          } else {
              LOG_ERROR("VideoDecoder::UpdateFrame: Lock2D failed. HRESULT = 0x%08X", hr);
          }
      }

      // Check 3: Software Fallback (Contiguous lock)
      BYTE* pData = nullptr;
      DWORD cbCurrentLength = 0;
      hr = pBuffer->Lock(&pData, nullptr, &cbCurrentLength);
      if (SUCCEEDED(hr)) {
          LOG_DEBUG("VideoDecoder::UpdateFrame: Path = SOFTWARE FALLBACK (Contiguous Lock). Length = %u", cbCurrentLength);

          if (m_videoWidth != m_videoTextureWidth || m_videoHeight != m_videoTextureHeight) {
              if (!ReallocateVideoTexture(m_videoWidth, m_videoHeight)) {
                  LOG_ERROR("VideoDecoder::UpdateFrame: ReallocateVideoTexture failed in secondary software path.");
                  pBuffer->Unlock();
                  return false;
              }
          }
          UINT32 rowPitch = m_videoWidth;
          pContext->UpdateSubresource(m_pVideoTexture.Get(), 0, nullptr, pData, rowPitch, 0);
          pBuffer->Unlock();
          m_pActiveSRV_Y = m_pVideoSRV_Y;
          m_pActiveSRV_UV = m_pVideoSRV_UV;
          return true;
      } else {
          LOG_ERROR("VideoDecoder::UpdateFrame: Buffer Lock failed. HRESULT = 0x%08X", hr);
      }

      return false;
  }
  ```

---

### Stage 7: Video Renderer Draw Stage
* **File**: `src/video_renderer.cpp`
* **Target Line**: ~206 (in `VideoRenderer::RenderVideoFrame`)
* **Logic**: Add debugging logs to verify SRV inputs, viewport properties, and the full-screen quad draw call.
* **Proposed Code Change**:
  * Note: Like `UpdateFrame`, `RenderVideoFrame` runs at high frequency. We use `LOG_DEBUG` to avoid spam.
  ```cpp
  HRESULT VideoRenderer::RenderVideoFrame(
      ID3D11ShaderResourceView* pVideoSRV_Y, 
      ID3D11ShaderResourceView* pVideoSRV_UV, 
      int textureWidth, 
      int textureHeight, 
      int videoWidth, 
      int videoHeight
  ) {
      LOG_DEBUG("VideoRenderer::RenderVideoFrame: Entry. SRVs: Y = 0x%p, UV = 0x%p. Texture = %dx%d. Video = %dx%d", 
          pVideoSRV_Y, pVideoSRV_UV, textureWidth, textureHeight, videoWidth, videoHeight);

      if (!m_pDeviceManager || !m_pSwapChainManager) return E_FAIL;
      auto d3dContext = m_pDeviceManager->GetContext();
      auto rtv = m_pSwapChainManager->GetRenderTargetView();

      if (!d3dContext || !rtv || !pVideoSRV_Y || !pVideoSRV_UV) {
          LOG_ERROR("VideoRenderer::RenderVideoFrame: Null rendering inputs. Context = 0x%p, RTV = 0x%p, Y = 0x%p, UV = 0x%p",
              d3dContext, rtv, pVideoSRV_Y, pVideoSRV_UV);
          return E_FAIL;
      }

      D3D11_VIEWPORT vp = { 0 };
      vp.Width = (float)m_pSwapChainManager->GetWidth();
      vp.Height = (float)m_pSwapChainManager->GetHeight();
      vp.MinDepth = 0.0f;
      vp.MaxDepth = 1.0f;

      LOG_DEBUG("VideoRenderer::RenderVideoFrame: Viewport configured: Width = %.1f, Height = %.1f", vp.Width, vp.Height);

      d3dContext->RSSetViewports(1, &vp);
      d3dContext->OMSetRenderTargets(1, &rtv, NULL);

      UpdateAspectRatioCB(textureWidth, textureHeight, videoWidth, videoHeight);

      d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
      d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
      
      d3dContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
      
      ID3D11ShaderResourceView* srvs[2] = { pVideoSRV_Y, pVideoSRV_UV };
      d3dContext->PSSetShaderResources(0, 2, srvs);
      d3dContext->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

      d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      d3dContext->IASetInputLayout(nullptr);

      LOG_DEBUG("VideoRenderer::RenderVideoFrame: Executing Draw(3, 0)...");
      d3dContext->Draw(3, 0);
      LOG_DEBUG("VideoRenderer::RenderVideoFrame: Draw executed successfully.");

      ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
      d3dContext->PSSetShaderResources(0, 2, nullSRVs);

      return S_OK;
  }
  ```

---

### Stage 8: Swap Chain Presentation
* **File**: `src/swap_chain_manager.cpp`
* **Target Line**: ~136 (in `SwapChainManager::Present`)
* **Logic**: Log the result HRESULT code of every `Present` call.
* **Proposed Code Change**:
  * Note: We use `LOG_DEBUG` here to prevent log spam in release builds. However, we use `LOG_ERROR` on failure to capture device removed/reset errors immediately in all builds.
  ```cpp
  HRESULT SwapChainManager::Present(int fpsLimit) {
      if (!m_swapChain) return E_POINTER;
      UINT syncInterval = (fpsLimit == 0) ? 1 : 0;
      HRESULT hr = m_swapChain->Present(syncInterval, 0);
      
      LOG_DEBUG("SwapChainManager::Present: Present called. SyncInterval = %u. HRESULT = 0x%08X", syncInterval, hr);
      if (FAILED(hr)) {
          LOG_ERROR("SwapChainManager::Present: Present failed! HRESULT = 0x%08X", hr);
      }
      return hr;
  }
  ```

---

### Stage 9: First Frame Milestone
* **Files**: `src/render_thread_controller.h` and `src/render_thread_controller.cpp`
* **Target Lines**:
  * In `src/render_thread_controller.h`: Declare private boolean member `m_firstFrameMilestoneLogged`.
  * In `src/render_thread_controller.cpp`:
    * Reset `m_firstFrameMilestoneLogged` to `false` during transitions (recreate window, change video, playlist rotation).
    * Set to `true` and log a special `LOG_INFO` milestone message when the first video frame is successfully updated AND presented.
* **Proposed Code Changes**:
  * **In `render_thread_controller.h`**:
    ```cpp
    // Add inside private section of RenderThreadController class (~line 56):
    bool m_firstFrameMilestoneLogged = false;
    ```
  * **In `render_thread_controller.cpp` (ThreadProc loop)**:
    * In section `1. Handle HWND Recreation` (~line 144):
      ```cpp
      m_screenCleared = false;
      m_firstFrameMilestoneLogged = false; // Reset milestone flag
      ```
    * In section `2. Handle Video/Shader Change` (~line 191):
      ```cpp
      m_screenCleared = false;
      m_firstFrameMilestoneLogged = false; // Reset milestone flag
      ```
    * In section `3. Handle Playlist Rotation` (~line 220):
      ```cpp
      m_screenCleared = false;
      m_firstFrameMilestoneLogged = false; // Reset milestone flag
      ```
    * In section `6. Update and Render Frame` (~line 301):
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
              
              if (frameUpdated && SUCCEEDED(hrPresent)) {
                  if (!m_firstFrameMilestoneLogged) {
                      LOG_INFO("==========================================================================");
                      LOG_INFO("MILESTONE: First video frame successfully decoded AND presented!");
                      LOG_INFO("  Video Resolution: %d x %d", m_decoder->GetVideoWidth(), m_decoder->GetVideoHeight());
                      LOG_INFO("  SwapChain Size:   %d x %d", m_swapChainManager->GetWidth(), m_swapChainManager->GetHeight());
                      LOG_INFO("==========================================================================");
                      m_firstFrameMilestoneLogged = true;
                  }
              }
          } else {
              if (waitTimeMs > 0.0) {
                  Timer::PreciseSleep(waitTimeMs < 1.0 ? 1.0 : waitTimeMs);
              }
          }
      }
      ```

---

## Log Level Strategy & Performance Considerations
To ensure the target machine logs are legible and do not cause performance issues, we apply the following log level rules:
1. **Startup/Initialization Stages** (WinMain, Device Creation, Decoder Initialization, Reader creation): Use `LOG_INFO` and `LOG_ERROR`. These occur only once or during error recovery, so they do not affect runtime performance.
2. **First Frame Milestone**: Use `LOG_INFO` as this is a crucial status marker that must be captured in both Debug and Release environments.
3. **High-Frequency Render Loop Stages** (UpdateFrame, Draw, Present): Use `LOG_DEBUG` (which is skipped in Release builds). For fatal errors within these stages (e.g., failed `Present` or failed buffer locks), elevate them to `LOG_ERROR` so they are always recorded. This shields Release runs from performance degradation and disk I/O bottlenecks while ensuring debugging info is fully available on local Dev setups.
