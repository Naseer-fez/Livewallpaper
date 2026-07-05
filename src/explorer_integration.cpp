#include "explorer_integration.h"
#include "utils.h"

ExplorerIntegration::ExplorerIntegration() {}

ExplorerIntegration::~ExplorerIntegration() {
    Shutdown();
}

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

void ExplorerIntegration::Shutdown() {
    m_isShuttingDown.store(true);
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    m_hWorkerW = nullptr;
    m_hShellDefView = nullptr;
    m_useLegacyWorkerW = true;
    if (m_hInstance) {
        UnregisterClassW(L"LiveWallpaperHostClass", m_hInstance);
    }
    LOG_INFO("Explorer Integration shut down.");
}

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

void ExplorerIntegration::Update() {
    DWORD currentTick = GetTickCount();
    if (currentTick - m_lastUpdateTick < 2000) return;
    m_lastUpdateTick = currentTick;

    HWND progman = FindWindowW(L"Progman", NULL);
    bool needsRecovery = false;

    if (!progman || !IsWindow(m_hWnd) || !IsWindow(m_hWorkerW)) {
        needsRecovery = true;
    } else {
        HWND parent = GetParent(m_hWnd);
        if (!parent || parent != m_hWorkerW) needsRecovery = true;
    }

    if (needsRecovery) {
        if (m_isRecovering.load()) return;

        // Implement exponential backoff retry mechanism (starting at 1s, doubling up to 30s)
        if (currentTick - m_lastRecoveryAttemptTick < m_retryIntervalMs) {
            // Not enough time has elapsed since the last recovery attempt
            return;
        }

        m_isRecovering.store(true);
        m_lastRecoveryAttemptTick = currentTick;
        LOG_WARN("Explorer restart or window invalidation detected! Triggering recovery (retry interval: %u ms)...", m_retryIntervalMs);
        Shutdown();
        
        if (Initialize(m_hInstance)) {
            LOG_INFO("Successfully recovered and re-injected wallpaper host.");
            m_retryIntervalMs = 1000; // Reset backoff on success
        } else {
            LOG_ERROR("Failed to recover wallpaper host.");
            m_retryIntervalMs = m_retryIntervalMs * 2;
            if (m_retryIntervalMs > 30000) {
                m_retryIntervalMs = 30000;
            }
        }
        m_isRecovering.store(false);
    } else if (!m_isRecovering.load()) {
        if (!m_useLegacyWorkerW && m_hShellDefView && IsWindow(m_hShellDefView) && IsWindow(m_hWnd)) {
            SetWindowPos(m_hWnd, m_hShellDefView, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}

LRESULT CALLBACK ExplorerIntegration::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    ExplorerIntegration* pThis = nullptr;
    if (message == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<ExplorerIntegration*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<ExplorerIntegration*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    switch (message) {
        case WM_DESTROY:
            if (pThis && (pThis->m_isRecovering.load() || pThis->m_isShuttingDown.load())) {
                LOG_INFO("ExplorerIntegration window destroyed due to recovery or shutdown.");
            } else {
                LOG_WARN("ExplorerIntegration window destroyed unexpectedly! Skipping PostQuitMessage to allow recovery.");
            }
            return 0;
            
        case WM_ERASEBKGND:
            return 1;
            
        case WM_DISPLAYCHANGE: {
            LOG_INFO("Display change detected. Resizing wallpaper host window.");
            int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
            int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);

            HWND hWndInsertAfter = HWND_BOTTOM;
            if (pThis && !pThis->m_useLegacyWorkerW && pThis->m_hShellDefView) {
                hWndInsertAfter = pThis->m_hShellDefView;
            }
            SetWindowPos(hWnd, hWndInsertAfter, x, y, cx, cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            return 0;
        }
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}
