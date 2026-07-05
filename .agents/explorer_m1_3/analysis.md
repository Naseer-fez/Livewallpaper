# Render Pipeline Diagnostic Instrumentation Analysis

This report outlines the proposed changes to implement Requirement R1 (Render Pipeline Diagnostic Instrumentation) for the Windows Live Wallpaper Engine.

The objective is to implement comprehensive, structured diagnostic logging throughout the entire rendering pipeline. This ensures that comparing logs from a working development machine with a failing target machine will immediately reveal the exact divergence point.

All logging uses the existing `Utils::Log` / `LogW` system (and their corresponding macros like `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, and `LOG_DEBUG`).

---

## 1. Overview of Affected Files

The following files must be modified to add the diagnostic logging:

1. `src/main.cpp` — Application entry and initial COM thread initialization.
2. `src/explorer_integration.cpp` — Shell desktop interaction, WorkerW discovery, host window creation, and injection.
3. `src/device_manager.cpp` — D3D11 device creation, graphic adapter capabilities detection, feature levels, and multithreading safety.
4. `src/swap_chain_manager.cpp` — DXGI swap chain initialization, RTV creation, resizing, and frame presentation.
5. `src/video_decoder.cpp` — Media Foundation startup, DXGI Device Manager creation, Source Reader fallback configurations, frame-by-frame decoding, and hardware/software texture copying.
6. `src/video_renderer.cpp` — Viewport configuration, shader binding, and actual draw calls.
7. `src/render_thread_controller.h` — Addition of milestone tracking member variable.
8. `src/render_thread_controller.cpp` — Render loop coordination, thread context COM initialization, component setup, and first-frame milestone verification.

---

## 2. Detailed Diagnostic Plan by Pipeline Stage

### Stage 2.1: Application Start & COM Initialization
* **File:** `src/main.cpp`
* **Function:** `WinMain`
* **Details:** Instrument the COM apartment initialization on the main application thread to track entry, exit, and HRESULT status.
* **Proposed Code Modification:**
  ```cpp
  // Around line 68
  Utils::InitializeLogging();
  LOG_INFO("LiveWallpaper main application starting.");

  LOG_INFO("CoInitializeEx entry: Attempting to initialize COM on main thread.");
  HRESULT hrCOM = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  if (SUCCEEDED(hrCOM)) {
      LOG_INFO("CoInitializeEx succeeded. Apartment-threaded COM initialized. HRESULT: 0x%08X", hrCOM);
  } else {
      LOG_ERROR("CoInitializeEx failed in main thread. HRESULT: 0x%08X", hrCOM);
  }
  ```

---

### Stage 2.2: Explorer Integration & Window Setup
* **File:** `src/explorer_integration.cpp`
* **Functions:** `Initialize`, `FindWorkerW`, `CreateHostWindow`, `InjectIntoDesktop`
* **Details:** Track desktop attachment steps, parent/child window handles, virtual screen sizes, and Windows Explorer window enumeration details.
* **Proposed Code Modification:**

  **`Initialize`:**
  ```cpp
  // Around line 10
  bool ExplorerIntegration::Initialize(HINSTANCE hInstance) {
      m_hInstance = hInstance;
      m_isShuttingDown.store(false);
      LOG_INFO("ExplorerIntegration::Initialize: Entering. Instance handle: 0x%p", hInstance);

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
      LOG_INFO("ExplorerIntegration::Initialize: Successfully initialized and injected.");
      return true;
  }
  ```

  **`FindWorkerW`:**
  ```cpp
  // Around line 50
  bool ExplorerIntegration::FindWorkerW() {
      LOG_INFO("ExplorerIntegration::FindWorkerW: Entering WorkerW search.");
      HWND progman = FindWindowW(L"Progman", NULL);
      if (!progman) {
          LOG_ERROR("ExplorerIntegration::FindWorkerW: Progman window handle not found.");
          return false;
      }
      LOG_INFO("ExplorerIntegration::FindWorkerW: Found Progman HWND: 0x%p", progman);

      LOG_INFO("ExplorerIntegration::FindWorkerW: Sending 0x052C to Progman. Timeout: 1000ms.");
      ULONG_PTR result = 0;
      LRESULT lResult = SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_ABORTIFHUNG, 1000, &result);
      if (lResult == 0) {
          LOG_WARN("ExplorerIntegration::FindWorkerW: SendMessageTimeoutW failed. Error: %u", GetLastError());
      } else {
          LOG_INFO("ExplorerIntegration::FindWorkerW: SendMessageTimeoutW succeeded. Message Result: %llu", result);
      }

      HWND shellDefView = NULL;
      HWND parentOfShell = NULL;
      HWND wallpaperWorkerW = NULL;

      shellDefView = FindWindowExW(progman, NULL, L"SHELLDLL_DefView", NULL);
      if (shellDefView) {
          parentOfShell = progman;
          LOG_INFO("ExplorerIntegration::FindWorkerW: SHELLDLL_DefView found in Progman: 0x%p", progman);
      } else {
          LOG_INFO("ExplorerIntegration::FindWorkerW: SHELLDLL_DefView not in Progman. Enumerating top-level WorkerW windows...");
          HWND workerW = FindWindowExW(NULL, NULL, L"WorkerW", NULL);
          while (workerW) {
              LOG_INFO("ExplorerIntegration::FindWorkerW: Enumerated WorkerW: 0x%p", workerW);
              shellDefView = FindWindowExW(workerW, NULL, L"SHELLDLL_DefView", NULL);
              if (shellDefView) {
                  parentOfShell = workerW;
                  LOG_INFO("ExplorerIntegration::FindWorkerW: SHELLDLL_DefView found in WorkerW: 0x%p", workerW);
                  break;
              }
              workerW = FindWindowExW(NULL, workerW, L"WorkerW", NULL);
          }
      }

      if (!shellDefView) {
          LOG_ERROR("ExplorerIntegration::FindWorkerW: SHELLDLL_DefView was not found in any WorkerW or Progman.");
          return false;
      }

      if (parentOfShell == progman) {
          m_hWorkerW = progman;
          m_hShellDefView = shellDefView;
          m_useLegacyWorkerW = false;
          LOG_WARN("ExplorerIntegration::FindWorkerW: Fallback to Progman triggered. parentOfShell == progman. Target HWND: 0x%p", m_hWorkerW);
      } else {
          LOG_INFO("ExplorerIntegration::FindWorkerW: Searching for secondary wallpaper WorkerW window...");
          HWND workerW = FindWindowExW(NULL, NULL, L"WorkerW", NULL);
          while (workerW) {
              LOG_INFO("ExplorerIntegration::FindWorkerW: Enumerating WorkerW (Pass 2): 0x%p", workerW);
              if (workerW != parentOfShell && !FindWindowExW(workerW, NULL, L"SHELLDLL_DefView", NULL)) {
                  wallpaperWorkerW = workerW;
                  LOG_INFO("ExplorerIntegration::FindWorkerW: Found empty WorkerW for wallpaper: 0x%p", workerW);
                  break;
              }
              workerW = FindWindowExW(NULL, workerW, L"WorkerW", NULL);
          }

          if (wallpaperWorkerW) {
              m_hWorkerW = wallpaperWorkerW;
              m_hShellDefView = NULL;
              m_useLegacyWorkerW = true;
              LOG_INFO("ExplorerIntegration::FindWorkerW: Dedicated wallpaper WorkerW assigned. HWND: 0x%p", m_hWorkerW);
          } else {
              m_hWorkerW = progman;
              m_hShellDefView = NULL;
              m_useLegacyWorkerW = false;
              LOG_WARN("ExplorerIntegration::FindWorkerW: Fallback to Progman. No empty WorkerW found. HWND: 0x%p", m_hWorkerW);
          }
      }

      LOG_INFO("ExplorerIntegration::FindWorkerW: Finished. Target WorkerW HWND: 0x%p", m_hWorkerW);
      return m_hWorkerW != nullptr;
  }
  ```

  **`CreateHostWindow`:**
  ```cpp
  // Around line 124
  bool ExplorerIntegration::CreateHostWindow(HINSTANCE hInstance) {
      LOG_INFO("ExplorerIntegration::CreateHostWindow: Entering. Target Parent HWND: 0x%p", m_hWorkerW);
      WNDCLASSEXW wcx = { 0 };
      if (!GetClassInfoExW(hInstance, L"LiveWallpaperHostClass", &wcx)) {
          LOG_INFO("ExplorerIntegration::CreateHostWindow: Registering 'LiveWallpaperHostClass' window class.");
          wcx.cbSize = sizeof(wcx);
          wcx.style = CS_HREDRAW | CS_VREDRAW;
          wcx.lpfnWndProc = WndProc;
          wcx.hInstance = hInstance;
          wcx.lpszClassName = L"LiveWallpaperHostClass";
          wcx.hCursor = LoadCursorW(NULL, IDC_ARROW);
          wcx.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
          if (!RegisterClassExW(&wcx)) {
              LOG_ERROR("ExplorerIntegration::CreateHostWindow: RegisterClassExW failed. Error: %d", GetLastError());
              return false;
          }
          LOG_INFO("ExplorerIntegration::CreateHostWindow: Window class successfully registered.");
      }

      int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
      int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
      int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
      int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);
      LOG_INFO("ExplorerIntegration::CreateHostWindow: Virtual screen boundaries: x = %d, y = %d, width = %d, height = %d", x, y, cx, cy);

      m_hWnd = CreateWindowExW(
          WS_EX_NOACTIVATE, L"LiveWallpaperHostClass", L"LiveWallpaperHost",
          WS_CHILD | WS_VISIBLE, x, y, cx, cy, m_hWorkerW, NULL, hInstance, this
      );

      if (!m_hWnd) {
          LOG_ERROR("ExplorerIntegration::CreateHostWindow: CreateWindowExW failed. Error: %d", GetLastError());
          return false;
      }

      LOG_INFO("ExplorerIntegration::CreateHostWindow: Window created successfully. HWND: 0x%p", m_hWnd);
      return true;
  }
  ```

  **`InjectIntoDesktop`:**
  ```cpp
  // Around line 153
  bool ExplorerIntegration::InjectIntoDesktop() {
      LOG_INFO("ExplorerIntegration::InjectIntoDesktop: Entering. HWND = 0x%p, WorkerW = 0x%p", m_hWnd, m_hWorkerW);
      if (!m_hWnd || !m_hWorkerW) {
          LOG_ERROR("ExplorerIntegration::InjectIntoDesktop: Invalid HWND or WorkerW pointer.");
          return false;
      }

      HWND currentParent = GetParent(m_hWnd);
      LOG_INFO("ExplorerIntegration::InjectIntoDesktop: Current parent is: 0x%p. Target parent: 0x%p", currentParent, m_hWorkerW);
      if (currentParent != m_hWorkerW) {
          LOG_INFO("ExplorerIntegration::InjectIntoDesktop: Reparenting host window to WorkerW.");
          HWND prevParent = SetParent(m_hWnd, m_hWorkerW);
          if (!prevParent && GetLastError() != 0) {
              LOG_ERROR("ExplorerIntegration::InjectIntoDesktop: SetParent failed. Error: %d", GetLastError());
              return false;
          }
          LOG_INFO("ExplorerIntegration::InjectIntoDesktop: SetParent call finished. Previous parent was: 0x%p", prevParent);
      }

      int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
      int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
      int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
      int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);

      HWND hWndInsertAfter = HWND_BOTTOM;
      if (!m_useLegacyWorkerW && m_hShellDefView) {
          hWndInsertAfter = m_hShellDefView;
      }
      LOG_INFO("ExplorerIntegration::InjectIntoDesktop: Setting window position. InsertAfter HWND: 0x%p", hWndInsertAfter);

      if (!SetWindowPos(m_hWnd, hWndInsertAfter, x, y, cx, cy, SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
          LOG_ERROR("ExplorerIntegration::InjectIntoDesktop: SetWindowPos failed. Error: %d", GetLastError());
          return false;
      }

      HWND finalParent = GetParent(m_hWnd);
      if (finalParent != m_hWorkerW) {
          LOG_ERROR("ExplorerIntegration::InjectIntoDesktop: Validation failed. Parent after SetWindowPos is 0x%p, expected 0x%p", finalParent, m_hWorkerW);
          return false;
      }
      LOG_INFO("ExplorerIntegration::InjectIntoDesktop: Host window successfully injected and verified. Final parent: 0x%p", finalParent);
      return true;
  }
  ```

---

### Stage 2.3: Render Thread & Subsystem Initialization
* **File:** `src/render_thread_controller.cpp`
* **Function:** `ThreadProc`
* **Details:** Track render thread startup, COM thread multi-threaded initialization, and step-by-step setup of D3D11 DeviceManager, SwapChainManager, VideoDecoder, and VideoRenderer.
* **Proposed Code Modification:**
  ```cpp
  // Around line 82
  void RenderThreadController::ThreadProc() {
      LOG_INFO("RenderThreadController::ThreadProc: Render thread started.");
      m_screenCleared = false;
      m_firstFrameMilestoneLogged = false; // Reset milestone on thread start

      LOG_INFO("RenderThreadController::ThreadProc: Initializing multi-threaded COM on render thread.");
      HRESULT hrCOM = CoInitializeEx(NULL, COINIT_MULTITHREADED);
      if (SUCCEEDED(hrCOM)) {
          LOG_INFO("RenderThreadController::ThreadProc: CoInitializeEx succeeded. HRESULT: 0x%08X", hrCOM);
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

          LOG_INFO("RenderThreadController::ThreadProc: Initializing DeviceManager.");
          if (m_deviceManager->Initialize()) {
              LOG_INFO("RenderThreadController::ThreadProc: DeviceManager initialized successfully. Device: 0x%p, Context: 0x%p", 
                  m_deviceManager->GetDevice(), m_deviceManager->GetContext());

              LOG_INFO("RenderThreadController::ThreadProc: Initializing SwapChainManager for HWND = 0x%p (%dx%d).", m_hWnd, w, h);
              if (m_swapChainManager->Initialize(m_deviceManager->GetDevice(), m_hWnd, w, h)) {
                  LOG_INFO("RenderThreadController::ThreadProc: SwapChainManager initialized successfully. SwapChain: 0x%p", m_swapChainManager->GetSwapChain());
                  
                  LOG_INFO("RenderThreadController::ThreadProc: Initializing VideoRenderer.");
                  if (m_videoRenderer->Initialize(m_deviceManager.get(), m_swapChainManager.get())) {
                      LOG_INFO("RenderThreadController::ThreadProc: VideoRenderer initialized successfully.");

                      if (IsShaderFile(m_videoPath)) {
                          LOG_INFO("RenderThreadController::ThreadProc: Wallpaper path is an HLSL shader. Loading FFI Shader Bridge.");
                          if (m_shaderBridge->Load()) {
                              HRESULT hr = m_shaderBridge->InitShaderHost(
                                  m_deviceManager->GetDevice(),
                                  m_deviceManager->GetContext(),
                                  m_videoPath,
                                  &m_shaderHost
                              );
                              if (FAILED(hr)) {
                                  LOG_ERROR("RenderThreadController::ThreadProc: InitShaderHost failed. HRESULT: 0x%08X", hr);
                              } else {
                                  LOG_INFO("RenderThreadController::ThreadProc: Rust Shader Host initialized successfully. Host pointer: 0x%p", m_shaderHost);
                              }
                          } else {
                              LOG_ERROR("RenderThreadController::ThreadProc: FFI Shader Bridge load failed.");
                          }
                      } else {
                          LOG_INFO("RenderThreadController::ThreadProc: Wallpaper path is a video. Initializing VideoDecoder.");
                          if (m_decoder->Initialize(m_deviceManager->GetDevice())) {
                              LOG_INFO("RenderThreadController::ThreadProc: VideoDecoder initialized successfully. Loading video: %ls", m_videoPath.c_str());
                              m_decoder->LoadVideo(m_videoPath);
                          } else {
                              LOG_ERROR("RenderThreadController::ThreadProc: VideoDecoder initialization failed.");
                          }
                      }
                  } else {
                      LOG_ERROR("RenderThreadController::ThreadProc: VideoRenderer initialization failed.");
                  }
              } else {
                  LOG_ERROR("RenderThreadController::ThreadProc: SwapChainManager initialization failed.");
              }
          } else {
              LOG_ERROR("RenderThreadController::ThreadProc: DeviceManager initialization failed.");
          }
      } else {
          LOG_WARN("RenderThreadController::ThreadProc: Skipping initial load. HWND: 0x%p, VideoPath: %ls", m_hWnd, m_videoPath.c_str());
      }
      ...
  ```

---

### Stage 2.4: D3D11 Device Manager Initialization & Active GPU Query
* **File:** `src/device_manager.cpp`
* **Function:** `Initialize`
* **Details:** Retrieve and log DXGI adapter description, VRAM size, PCI Vendor ID, Device ID, selected feature level, and multi-threading protection HRESULTs.
* **Proposed Code Modification:**
  ```cpp
  // Around line 10
  bool DeviceManager::Initialize() {
      LOG_INFO("DeviceManager::Initialize: Entering D3D11 device initialization.");

      // Retrieve DXGI Adapter details
      Microsoft::WRL::ComPtr<IDXGIFactory1> dxgiFactory;
      if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), &dxgiFactory))) {
          Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
          if (SUCCEEDED(dxgiFactory->EnumAdapters1(0, &adapter))) {
              DXGI_ADAPTER_DESC1 desc;
              if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                  LOG_INFO("DeviceManager::Initialize: Active graphics adapter: %ls", desc.Description);
                  LOG_INFO("DeviceManager::Initialize: Vendor ID: 0x%04X, Device ID: 0x%04X, Revision: 0x%04X", desc.VendorId, desc.DeviceId, desc.Revision);
                  LOG_INFO("DeviceManager::Initialize: Dedicated Video Memory: %zu MB, Shared System Memory: %zu MB",
                      desc.DedicatedVideoMemory / (1024 * 1024),
                      desc.SharedSystemMemory / (1024 * 1024));
              } else {
                  LOG_WARN("DeviceManager::Initialize: Failed to query DXGI adapter description.");
              }
          } else {
              LOG_WARN("DeviceManager::Initialize: Failed to enumerate primary adapter.");
          }
      } else {
          LOG_WARN("DeviceManager::Initialize: Failed to create DXGI Factory for adapter query.");
      }

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

      LOG_INFO("DeviceManager::Initialize: Calling D3D11CreateDevice with DRIVER_TYPE_HARDWARE.");
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
          LOG_WARN("DeviceManager::Initialize: Hardware D3D11 Device creation failed. HRESULT = 0x%08X. Falling back to WARP.", hr);
          LOG_INFO("DeviceManager::Initialize: Calling D3D11CreateDevice with DRIVER_TYPE_WARP.");
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
          LOG_ERROR("DeviceManager::Initialize: Failed to create D3D11 Device (HRESULT = 0x%08X)", hr);
          return false;
      }

      const char* featureLevelStr = "Unknown";
      switch (supportedLevel) {
          case D3D_FEATURE_LEVEL_11_1: featureLevelStr = "11_1"; break;
          case D3D_FEATURE_LEVEL_11_0: featureLevelStr = "11_0"; break;
          case D3D_FEATURE_LEVEL_10_1: featureLevelStr = "10_1"; break;
          case D3D_FEATURE_LEVEL_10_0: featureLevelStr = "10_0"; break;
      }
      LOG_INFO("DeviceManager::Initialize: D3D11 device created successfully. Feature Level: %s. HRESULT: 0x%08X", featureLevelStr, hr);

      // Protect context for multithreading (needed by Media Foundation)
      Microsoft::WRL::ComPtr<ID3D10Multithread> pMultithread;
      HRESULT hrMT = m_d3dDevice.As(&pMultithread);
      if (SUCCEEDED(hrMT)) {
          pMultithread->SetMultithreadProtected(TRUE);
          LOG_INFO("DeviceManager::Initialize: Multithread protection enabled on D3D11 device context.");
      } else {
          LOG_WARN("DeviceManager::Initialize: Failed to acquire ID3D10Multithread interface from device. HRESULT: 0x%08X", hrMT);
      }

      LOG_INFO("DeviceManager::Initialize: DeviceManager successfully initialized.");
      return true;
  }
  ```

---

### Stage 2.5: DXGI Swap Chain Manager & Frame Presentation
* **File:** `src/swap_chain_manager.cpp`
* **Functions:** `Initialize`, `CreateSwapChain`, `CreateRenderTargetView`, `Resize`, `Present`
* **Details:** Log descriptors, dimensions, formats, buffer counts, scaling modes, and the HRESULT code of *every* SwapChain Present call.
* **Proposed Code Modification:**

  **`Initialize`:**
  ```cpp
  // Around line 10
  bool SwapChainManager::Initialize(ID3D11Device* device, HWND hWnd, int width, int height) {
      m_hWnd = hWnd;
      m_width = width;
      m_height = height;
      LOG_INFO("SwapChainManager::Initialize: Entering. HWND = 0x%p, Dimensions = %dx%d", hWnd, width, height);

      if (!CreateSwapChain(device)) {
          LOG_ERROR("SwapChainManager::Initialize: Failed to create Swap Chain.");
          Shutdown();
          return false;
      }
      if (!CreateRenderTargetView(device)) {
          LOG_ERROR("SwapChainManager::Initialize: Failed to create Render Target View.");
          Shutdown();
          return false;
      }

      LOG_INFO("SwapChainManager::Initialize: SwapChainManager successfully initialized.");
      return true;
  }
  ```

  **`CreateSwapChain`:**
  ```cpp
  // Around line 42
  bool SwapChainManager::CreateSwapChain(ID3D11Device* device) {
      if (!device) {
          LOG_ERROR("SwapChainManager::CreateSwapChain: Null D3D11 device pointer.");
          return false;
      }
      LOG_INFO("SwapChainManager::CreateSwapChain: Querying DXGI interfaces.");

      Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
      HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
      if (FAILED(hr)) {
          LOG_ERROR("SwapChainManager::CreateSwapChain: QueryInterface for IDXGIDevice failed. HRESULT = 0x%08X", hr);
          return false;
      }

      Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
      hr = dxgiDevice->GetAdapter(&dxgiAdapter);
      if (FAILED(hr)) {
          LOG_ERROR("SwapChainManager::CreateSwapChain: GetAdapter failed. HRESULT = 0x%08X", hr);
          return false;
      }

      Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
      hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
      if (FAILED(hr)) {
          LOG_ERROR("SwapChainManager::CreateSwapChain: GetParent for IDXGIFactory2 failed. HRESULT = 0x%08X", hr);
          return false;
      }

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

      LOG_INFO("SwapChainManager::CreateSwapChain: Configured DXGI_SWAP_CHAIN_DESC1: Width = %u, Height = %u, Format = %u, BufferCount = %u, SwapEffect = %u", 
          scd.Width, scd.Height, scd.Format, scd.BufferCount, scd.SwapEffect);

      hr = dxgiFactory->CreateSwapChainForHwnd(
          device,
          m_hWnd,
          &scd,
          &fsd,
          NULL,
          &m_swapChain
      );

      if (FAILED(hr)) {
          LOG_ERROR("SwapChainManager::CreateSwapChain: CreateSwapChainForHwnd failed. HRESULT = 0x%08X", hr);
          return false;
      }

      LOG_INFO("SwapChainManager::CreateSwapChain: Swap chain created successfully. SwapChain pointer: 0x%p", m_swapChain.Get());
      return true;
  }
  ```

  **`CreateRenderTargetView`:**
  ```cpp
  // Around line 93
  bool SwapChainManager::CreateRenderTargetView(ID3D11Device* device) {
      if (!device || !m_swapChain) {
          LOG_ERROR("SwapChainManager::CreateRenderTargetView: Invalid device or swap chain pointer.");
          return false;
      }
      LOG_INFO("SwapChainManager::CreateRenderTargetView: Retrieving back buffer.");

      Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
      HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
      if (FAILED(hr)) {
          LOG_ERROR("SwapChainManager::CreateRenderTargetView: GetBuffer failed. HRESULT = 0x%08X", hr);
          return false;
      }

      hr = device->CreateRenderTargetView(backBuffer.Get(), NULL, &m_renderTargetView);
      if (FAILED(hr)) {
          LOG_ERROR("SwapChainManager::CreateRenderTargetView: CreateRenderTargetView failed. HRESULT = 0x%08X", hr);
          return false;
      }

      LOG_INFO("SwapChainManager::CreateRenderTargetView: RenderTargetView created successfully. RTV: 0x%p", m_renderTargetView.Get());
      return true;
  }
  ```

  **`Resize`:**
  ```cpp
  // Around line 106
  bool SwapChainManager::Resize(ID3D11Device* device, ID3D11DeviceContext* context, int width, int height) {
      if (width <= 0 || height <= 0) return false;
      if (width == m_width && height == m_height) return true;

      LOG_INFO("SwapChainManager::Resize: Attempting resize. Width: %d, Height: %d (Previous: %dx%d)", width, height, m_width, m_height);
      m_width = width;
      m_height = height;

      if (!m_swapChain) {
          LOG_ERROR("SwapChainManager::Resize: Swap chain is null. Cannot resize.");
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

      if (FAILED(hr)) {
          LOG_ERROR("SwapChainManager::Resize: ResizeBuffers failed. HRESULT = 0x%08X", hr);
          return false;
      }

      LOG_INFO("SwapChainManager::Resize: ResizeBuffers succeeded. Re-creating RenderTargetView.");
      return CreateRenderTargetView(device);
  }
  ```

  **`Present`:**
  ```cpp
  // Around line 136
  HRESULT SwapChainManager::Present(int fpsLimit) {
      if (!m_swapChain) {
          LOG_ERROR("SwapChainManager::Present: SwapChain is null.");
          return E_POINTER;
      }
      UINT syncInterval = (fpsLimit == 0) ? 1 : 0;
      HRESULT hr = m_swapChain->Present(syncInterval, 0);
      
      // Use LOG_DEBUG to prevent high disk I/O write spam, but output errors as LOG_ERROR
      if (FAILED(hr)) {
          LOG_ERROR("SwapChainManager::Present: Present failed. HRESULT = 0x%08X", hr);
      } else {
          LOG_DEBUG("SwapChainManager::Present: Present succeeded. HRESULT = S_OK");
      }
      return hr;
  }
  ```

---

### Stage 2.6: Media Foundation Video Decoder Initialization & Loading Fallback Chain
* **File:** `src/video_decoder.cpp`
* **Functions:** `Initialize`, `LoadVideo`, `UpdateFrame`
* **Details:** Instrument the Media Foundation startup, DXGI Device Manager reset, 4-stage source reader fallback attempts, video format metadata checks, and frame-by-frame decoding paths (hardware vs software updates).
* **Proposed Code Modification:**

  **`Initialize`:**
  ```cpp
  // Around line 12
  bool VideoDecoder::Initialize(ID3D11Device* pDevice) {
      if (!pDevice) {
          LOG_ERROR("VideoDecoder::Initialize: Received null D3D11 device.");
          return false;
      }
      m_pDevice = pDevice;
      LOG_INFO("VideoDecoder::Initialize: Entering. Device: 0x%p", pDevice);

      Microsoft::WRL::ComPtr<ID3D10Multithread> pMultithread;
      HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pMultithread));
      if (SUCCEEDED(hr)) {
          pMultithread->SetMultithreadProtected(TRUE);
          LOG_INFO("VideoDecoder::Initialize: Multithread protection enabled on D3D11 device.");
      } else {
          LOG_WARN("VideoDecoder::Initialize: Failed to enable D3D11 multithread protection. DXVA2 may be unstable. HRESULT: 0x%08X", hr);
      }

      LOG_INFO("VideoDecoder::Initialize: Launching Media Foundation (MFStartup).");
      hr = MFStartup(MF_VERSION);
      if (FAILED(hr)) {
          LOG_ERROR("VideoDecoder::Initialize: MFStartup failed. HRESULT: 0x%08X", hr);
          return false;
      }

      LOG_INFO("VideoDecoder::Initialize: Creating DXGI Device Manager.");
      hr = MFCreateDXGIDeviceManager(&m_deviceResetToken, &m_pDeviceManager);
      if (FAILED(hr)) {
          LOG_ERROR("VideoDecoder::Initialize: MFCreateDXGIDeviceManager failed. HRESULT: 0x%08X", hr);
          MFShutdown();
          return false;
      }

      LOG_INFO("VideoDecoder::Initialize: Resetting DXGI Device Manager with device. Token = %u", m_deviceResetToken);
      hr = m_pDeviceManager->ResetDevice(m_pDevice, m_deviceResetToken);
      if (FAILED(hr)) {
          LOG_ERROR("VideoDecoder::Initialize: IMFDXGIDeviceManager::ResetDevice failed. HRESULT: 0x%08X", hr);
          m_pDeviceManager.Reset();
          MFShutdown();
          return false;
      }

      LOG_INFO("VideoDecoder::Initialize: Successfully initialized with hardware acceleration.");
      return true;
  }
  ```

  **`LoadVideo`:**
  ```cpp
  // Around line 64
  bool VideoDecoder::LoadVideo(const std::wstring& filePath) {
      CloseVideo();
      m_filePath = filePath;
      LOG_INFO("VideoDecoder::LoadVideo: Entering. File path: %ls", filePath.c_str());

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
          LOG_INFO("VideoDecoder::LoadVideo: Attempting fallback combination: '%s' (useD3DManager: %d, useVideoProcessing: %d)",
              opt.name, opt.useD3DManager, opt.useVideoProcessing);

          if (opt.useD3DManager && !m_pDeviceManager) {
              LOG_WARN("VideoDecoder::LoadVideo: Skipping '%s' (DXGI Device Manager not initialized).", opt.name);
              continue;
          }

          Microsoft::WRL::ComPtr<IMFAttributes> pAttributes;
          UINT32 attrCount = 0;
          if (opt.useD3DManager) attrCount++;
          if (opt.useVideoProcessing) attrCount++;

          if (attrCount > 0) {
              hr = MFCreateAttributes(&pAttributes, attrCount);
              if (FAILED(hr)) {
                  LOG_WARN("VideoDecoder::LoadVideo: MFCreateAttributes failed for combination '%s'. HRESULT: 0x%08X", opt.name, hr);
                  continue;
              }
              if (opt.useD3DManager) {
                  pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_pDeviceManager.Get());
              }
              if (opt.useVideoProcessing) {
                  pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
              }
          }

          hr = MFCreateSourceReaderFromURL(m_filePath.c_str(), pAttributes.Get(), &m_pSourceReader);
          if (FAILED(hr)) {
              LOG_WARN("VideoDecoder::LoadVideo: MFCreateSourceReaderFromURL failed for combination '%s'. HRESULT: 0x%08X", opt.name, hr);
              m_pSourceReader.Reset();
              continue;
          }

          hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
          if (FAILED(hr)) {
              LOG_WARN("VideoDecoder::LoadVideo: SetStreamSelection(All Streams, FALSE) failed for combination '%s'. HRESULT: 0x%08X", opt.name, hr);
              m_pSourceReader.Reset();
              continue;
          }

          hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
          if (FAILED(hr)) {
              LOG_WARN("VideoDecoder::LoadVideo: SetStreamSelection(First Video, TRUE) failed for combination '%s'. HRESULT: 0x%08X", opt.name, hr);
              m_pSourceReader.Reset();
              continue;
          }

          Microsoft::WRL::ComPtr<IMFMediaType> pType;
          hr = MFCreateMediaType(&pType);
          if (FAILED(hr)) {
              LOG_WARN("VideoDecoder::LoadVideo: MFCreateMediaType failed for combination '%s'. HRESULT: 0x%08X", opt.name, hr);
              m_pSourceReader.Reset();
              continue;
          }

          hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
          if (FAILED(hr)) {
              LOG_WARN("VideoDecoder::LoadVideo: SetGUID(Major Type) failed for combination '%s'. HRESULT: 0x%08X", opt.name, hr);
              m_pSourceReader.Reset();
              continue;
          }

          hr = pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
          if (FAILED(hr)) {
              LOG_WARN("VideoDecoder::LoadVideo: SetGUID(Subtype NV12) failed for combination '%s'. HRESULT: 0x%08X", opt.name, hr);
              m_pSourceReader.Reset();
              continue;
          }

          hr = m_pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType.Get());
          if (FAILED(hr)) {
              LOG_WARN("VideoDecoder::LoadVideo: SetCurrentMediaType to NV12 failed for combination '%s'. HRESULT: 0x%08X", opt.name, hr);
              m_pSourceReader.Reset();
              continue;
          }

          LOG_INFO("VideoDecoder::LoadVideo: Successfully loaded video with combination: '%s'", opt.name);
          initialized = true;
          break;
      }

      if (!initialized || !m_pSourceReader) {
          LOG_ERROR_W(L"VideoDecoder::LoadVideo: All Source Reader fallback combinations failed for: %ls", m_filePath.c_str());
          return false;
      }

      // Metadata retrieval
      Microsoft::WRL::ComPtr<IMFMediaType> pCurrentType;
      hr = m_pSourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType);
      if (FAILED(hr)) {
          LOG_ERROR("VideoDecoder::LoadVideo: GetCurrentMediaType failed. HRESULT: 0x%08X", hr);
          return false;
      }

      UINT32 width = 0, height = 0;
      hr = MFGetAttributeSize(pCurrentType.Get(), MF_MT_FRAME_SIZE, &width, &height);
      if (FAILED(hr)) {
          LOG_ERROR("VideoDecoder::LoadVideo: Failed to retrieve video frame dimensions. HRESULT: 0x%08X", hr);
          return false;
      }
      m_videoWidth = width;
      m_videoHeight = height;

      // Extract and log video codec subtype
      GUID subType = { 0 };
      if (SUCCEEDED(pCurrentType->GetGUID(MF_MT_SUBTYPE, &subType))) {
          const char* codecStr = "Unknown Codec";
          if (subType == MFVideoFormat_H264) codecStr = "H.264 (AVC)";
          else if (subType == MFVideoFormat_HEVC) codecStr = "H.265 (HEVC)";
          else if (subType == MFVideoFormat_WMV3) codecStr = "WMV3";
          else if (subType == MFVideoFormat_MP4V) codecStr = "MP4V";
          else if (subType == MFVideoFormat_NV12) codecStr = "NV12 (Raw)";
          LOG_INFO("VideoDecoder::LoadVideo: Video format codec detected: %s", codecStr);
      }

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

      LOG_INFO("VideoDecoder::LoadVideo: Background decoding thread started successfully.");
      return true;
  }
  ```

  **`UpdateFrame`:**
  ```cpp
  // Around line 387
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
          if (!frontSample) break;

          LONGLONG hnsTimestamp = 0;
          if (FAILED(frontSample->GetSampleTime(&hnsTimestamp))) {
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

      if (!hasNewFrame || !pSelectedSample) return false;

      m_playbackTimer.Reset();

      Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer;
      HRESULT hr = pSelectedSample->GetBufferByIndex(0, &pBuffer);
      if (FAILED(hr)) {
          LOG_ERROR("VideoDecoder::UpdateFrame: GetBufferByIndex failed. HRESULT: 0x%08X", hr);
          return false;
      }

      // Hardware Path Check
      Microsoft::WRL::ComPtr<IMFDXGIBuffer> pDXGIBuffer;
      hr = pBuffer.As(&pDXGIBuffer);
      if (SUCCEEDED(hr)) {
          LOG_DEBUG("VideoDecoder::UpdateFrame: DXGI Buffer acquired (Hardware path).");
          Microsoft::WRL::ComPtr<ID3D11Texture2D> pMFTexture;
          hr = pDXGIBuffer->GetResource(IID_PPV_ARGS(&pMFTexture));
          if (SUCCEEDED(hr)) {
              D3D11_TEXTURE2D_DESC mfDesc;
              pMFTexture->GetDesc(&mfDesc);
              
              if (mfDesc.Width != m_videoTextureWidth || mfDesc.Height != m_videoTextureHeight) {
                  LOG_INFO("VideoDecoder::UpdateFrame: Hardware size changed. Reallocating local texture to: %dx%d", mfDesc.Width, mfDesc.Height);
                  if (!ReallocateVideoTexture(mfDesc.Width, mfDesc.Height)) {
                      LOG_ERROR("VideoDecoder::UpdateFrame: Hardware path reallocation failed.");
                      return false;
                  }
              }

              UINT subresourceIndex = 0;
              pDXGIBuffer->GetSubresourceIndex(&subresourceIndex);

              LOG_DEBUG("VideoDecoder::UpdateFrame: Copying subresource region. Subresource: %u", subresourceIndex);
              pContext->CopySubresourceRegion(
                  m_pVideoTexture.Get(), 0, 0, 0, 0,
                  pMFTexture.Get(), subresourceIndex, nullptr
              );
              
              m_pActiveSRV_Y = m_pVideoSRV_Y;
              m_pActiveSRV_UV = m_pVideoSRV_UV;
              return true;
          } else {
              LOG_WARN("VideoDecoder::UpdateFrame: GetResource failed in hardware path. HRESULT: 0x%08X. Falling back to software.", hr);
          }
      }

      // Software Path 1 (2D Buffer)
      Microsoft::WRL::ComPtr<IMF2DBuffer> p2DBuffer;
      hr = pBuffer.As(&p2DBuffer);
      if (SUCCEEDED(hr)) {
          LOG_DEBUG("VideoDecoder::UpdateFrame: IMF2DBuffer acquired (Software 2D path).");
          if (m_videoWidth != m_videoTextureWidth || m_videoHeight != m_videoTextureHeight) {
              LOG_INFO("VideoDecoder::UpdateFrame: Software size changed. Reallocating local texture to: %dx%d", m_videoWidth, m_videoHeight);
              if (!ReallocateVideoTexture(m_videoWidth, m_videoHeight)) {
                  LOG_ERROR("VideoDecoder::UpdateFrame: Software 2D reallocation failed.");
                  return false;
              }
          }
          BYTE* pScanline0 = nullptr;
          LONG pitch = 0;
          hr = p2DBuffer->Lock2D(&pScanline0, &pitch);
          if (SUCCEEDED(hr)) {
              LOG_DEBUG("VideoDecoder::UpdateFrame: Updating subresource (2D). Pitch: %d", pitch);
              pContext->UpdateSubresource(m_pVideoTexture.Get(), 0, nullptr, pScanline0, pitch, 0);
              p2DBuffer->Unlock2D();
              m_pActiveSRV_Y = m_pVideoSRV_Y;
              m_pActiveSRV_UV = m_pVideoSRV_UV;
              return true;
          } else {
              LOG_ERROR("VideoDecoder::UpdateFrame: Lock2D failed. HRESULT: 0x%08X", hr);
          }
      }

      // Software Path 2 (1D Buffer Contiguous)
      LOG_DEBUG("VideoDecoder::UpdateFrame: Falling back to contiguous lock (Software 1D path).");
      BYTE* pData = nullptr;
      DWORD cbCurrentLength = 0;
      hr = pBuffer->Lock(&pData, nullptr, &cbCurrentLength);
      if (SUCCEEDED(hr)) {
          LOG_DEBUG("VideoDecoder::UpdateFrame: Contiguous Lock succeeded. Length: %u", cbCurrentLength);
          if (m_videoWidth != m_videoTextureWidth || m_videoHeight != m_videoTextureHeight) {
              LOG_INFO("VideoDecoder::UpdateFrame: Reallocating local texture for 1D copy.");
              if (!ReallocateVideoTexture(m_videoWidth, m_videoHeight)) {
                  LOG_ERROR("VideoDecoder::UpdateFrame: Software 1D reallocation failed.");
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
          LOG_ERROR("VideoDecoder::UpdateFrame: Contiguous Lock failed. HRESULT: 0x%08X", hr);
      }

      LOG_ERROR("VideoDecoder::UpdateFrame: Frame update failed on all paths.");
      return false;
  }
  ```

---

### Stage 2.7: Video Render Frame
* **File:** `src/video_renderer.cpp`
* **Function:** `RenderVideoFrame`
* **Details:** Track drawing entry, SRV binding pointers, mapped constant buffers, viewport dimensions, primitive topology, and the final draw call.
* **Proposed Code Modification:**
  ```cpp
  // Around line 206
  HRESULT VideoRenderer::RenderVideoFrame(
      ID3D11ShaderResourceView* pVideoSRV_Y, 
      ID3D11ShaderResourceView* pVideoSRV_UV, 
      int textureWidth, 
      int textureHeight, 
      int videoWidth, 
      int videoHeight
  ) {
      LOG_DEBUG("VideoRenderer::RenderVideoFrame: Entering. Y SRV: 0x%p, UV SRV: 0x%p, Tex size: %dx%d, Video size: %dx%d",
          pVideoSRV_Y, pVideoSRV_UV, textureWidth, textureHeight, videoWidth, videoHeight);

      if (!m_pDeviceManager || !m_pSwapChainManager) {
          LOG_ERROR("VideoRenderer::RenderVideoFrame: Null managers.");
          return E_FAIL;
      }
      auto d3dContext = m_pDeviceManager->GetContext();
      auto rtv = m_pSwapChainManager->GetRenderTargetView();

      if (!d3dContext || !rtv || !pVideoSRV_Y || !pVideoSRV_UV) {
          LOG_ERROR("VideoRenderer::RenderVideoFrame: Invalid D3D context, RTV, or SRV pointers.");
          return E_FAIL;
      }

      D3D11_VIEWPORT vp = { 0 };
      vp.Width = (float)m_pSwapChainManager->GetWidth();
      vp.Height = (float)m_pSwapChainManager->GetHeight();
      vp.MinDepth = 0.0f;
      vp.MaxDepth = 1.0f;
      
      LOG_DEBUG("VideoRenderer::RenderVideoFrame: Setting Viewport to %f x %f and binding RenderTarget.", vp.Width, vp.Height);
      d3dContext->RSSetViewports(1, &vp);
      d3dContext->OMSetRenderTargets(1, &rtv, NULL);

      LOG_DEBUG("VideoRenderer::RenderVideoFrame: Updating aspect ratio constant buffer.");
      UpdateAspectRatioCB(textureWidth, textureHeight, videoWidth, videoHeight);

      LOG_DEBUG("VideoRenderer::RenderVideoFrame: Setting Vertex & Pixel Shaders and bindings.");
      d3dContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
      d3dContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
      d3dContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
      
      ID3D11ShaderResourceView* srvs[2] = { pVideoSRV_Y, pVideoSRV_UV };
      d3dContext->PSSetShaderResources(0, 2, srvs);
      d3dContext->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

      d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      d3dContext->IASetInputLayout(nullptr);

      LOG_DEBUG("VideoRenderer::RenderVideoFrame: Triggering Draw(3, 0).");
      d3dContext->Draw(3, 0);

      ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
      d3dContext->PSSetShaderResources(0, 2, nullSRVs);

      LOG_DEBUG("VideoRenderer::RenderVideoFrame: Drawing complete.");
      return S_OK;
  }
  ```

---

### Stage 2.8: First Frame Milestone Logging
To log the exact moment when the very first frame is successfully decoded AND presented, we need to track this milestone event.
* **File:** `src/render_thread_controller.h`
  Add the following private member variable:
  ```cpp
      bool m_firstFrameMilestoneLogged = false;
  ```
* **File:** `src/render_thread_controller.cpp`
  Under the render loop's `UpdateFrame` / `Present` call (around line 301):
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
                      
                      // MILESTONE DETECTED: First frame decoded AND presented
                      if (frameUpdated && SUCCEEDED(hrPresent) && !m_firstFrameMilestoneLogged) {
                          m_firstFrameMilestoneLogged = true;
                          LOG_INFO("MILESTONE: First video frame successfully decoded and presented. HWND = 0x%p, Resolution: %dx%d, Video: %ls",
                              m_hWnd, m_decoder->GetVideoWidth(), m_decoder->GetVideoHeight(), m_videoPath.c_str());
                      }
                  }
                  ...
  ```
