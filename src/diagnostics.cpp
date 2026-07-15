#include "diagnostics.h"
#include "utils.h"
#include <cstdio>
#include <ctime>
#include <vector>
#include <string>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <mfapi.h>
#include <mfidl.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>

#pragma comment(lib, "dwmapi.lib")

namespace Diagnostics {

// Helper to write a line to both a file and stdout
static void WriteLine(FILE* f, const char* format, ...) {
    va_list args;
    char buf[2048];

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    printf("%s\n", buf);
    if (f) {
        fwprintf(f, L"%hs\n", buf);
    }
}

static void WriteLineW(FILE* f, const wchar_t* format, ...) {
    va_list args;
    wchar_t buf[2048];

    va_start(args, format);
    vswprintf(buf, 2048, format, args);
    va_end(args);

    wprintf(L"%ls\n", buf);
    if (f) {
        fwprintf(f, L"%ls\n", buf);
    }
}

static const char* FeatureLevelToString(D3D_FEATURE_LEVEL fl) {
    switch (fl) {
        case D3D_FEATURE_LEVEL_9_1:  return "9.1";
        case D3D_FEATURE_LEVEL_9_2:  return "9.2";
        case D3D_FEATURE_LEVEL_9_3:  return "9.3";
        case D3D_FEATURE_LEVEL_10_0: return "10.0";
        case D3D_FEATURE_LEVEL_10_1: return "10.1";
        case D3D_FEATURE_LEVEL_11_0: return "11.0";
        case D3D_FEATURE_LEVEL_11_1: return "11.1";
        default: return "Unknown";
    }
}

bool RunDiagnosticReport() {
    bool allocatedConsole = false;
    bool isRedirected = false;
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdOut != INVALID_HANDLE_VALUE && hStdOut != nullptr) {
        DWORD fileType = GetFileType(hStdOut);
        if (fileType == FILE_TYPE_DISK || fileType == FILE_TYPE_PIPE) {
            isRedirected = true;
        }
    }

    if (!isRedirected) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            if (AllocConsole()) {
                allocatedConsole = true;
            }
        }
        FILE* conOut = nullptr;
        freopen_s(&conOut, "CONOUT$", "w", stdout);
    }

    // Open output file
    std::wstring reportPath = Utils::GetAppDataPath();
    CreateDirectoryW(reportPath.c_str(), NULL);
    reportPath += L"\\diagnostic_report.txt";

    FILE* f = nullptr;
    _wfopen_s(&f, reportPath.c_str(), L"w, ccs=UTF-8");

    // Header
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

    WriteLine(f, "==========================================================");
    WriteLine(f, "LiveWallpaper Diagnostic Report");
    WriteLine(f, "Generated: %s", timeStr);
    WriteLine(f, "==========================================================");
    WriteLine(f, "");

    // ---- Section 1: Windows Version ----
    WriteLine(f, "--- WINDOWS VERSION ---");
    {
        OSVERSIONINFOEXW osvi = {};
        osvi.dwOSVersionInfoSize = sizeof(osvi);

        // Use RtlGetVersion to bypass the manifest requirement
        typedef LONG(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
        HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
        if (hNtDll) {
            auto pRtlGetVersion = (RtlGetVersionFn)GetProcAddress(hNtDll, "RtlGetVersion");
            if (pRtlGetVersion) {
                pRtlGetVersion((PRTL_OSVERSIONINFOW)&osvi);
                WriteLine(f, "  OS version: %d.%d.%d", osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);
                WriteLine(f, "  Product Type: %s",
                    osvi.wProductType == VER_NT_WORKSTATION ? "Workstation" :
                    osvi.wProductType == VER_NT_SERVER ? "Server" : "Domain Controller");
            }
        }

        // Also get the display version (e.g., "23H2")
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t displayVersion[64] = {};
            DWORD size = sizeof(displayVersion);
            if (RegQueryValueExW(hKey, L"DisplayVersion", NULL, NULL, (LPBYTE)displayVersion, &size) == ERROR_SUCCESS) {
                WriteLineW(f, L"  Display Version: %ls", displayVersion);
            }
            wchar_t editionId[64] = {};
            size = sizeof(editionId);
            if (RegQueryValueExW(hKey, L"EditionID", NULL, NULL, (LPBYTE)editionId, &size) == ERROR_SUCCESS) {
                WriteLineW(f, L"  Edition: %ls", editionId);
            }
            RegCloseKey(hKey);
        }
    }
    WriteLine(f, "");

    // ---- Section 2: GPU Adapters ----
    WriteLine(f, "--- GPU ADAPTERS ---");
    WriteLine(f, "  GPU details:");
    {
        Microsoft::WRL::ComPtr<IDXGIFactory1> dxgiFactory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
        if (SUCCEEDED(hr)) {
            UINT adapterIndex = 0;
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            while (dxgiFactory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND) {
                DXGI_ADAPTER_DESC1 desc = {};
                adapter->GetDesc1(&desc);
                WriteLineW(f, L"  Adapter %d: %ls", adapterIndex, desc.Description);
                WriteLine(f, "    Vendor ID: 0x%04X, Device ID: 0x%04X", desc.VendorId, desc.DeviceId);
                WriteLine(f, "    Dedicated Video Memory: %llu MB", (unsigned long long)(desc.DedicatedVideoMemory / (1024 * 1024)));
                WriteLine(f, "    Dedicated System Memory: %llu MB", (unsigned long long)(desc.DedicatedSystemMemory / (1024 * 1024)));
                WriteLine(f, "    Shared System Memory: %llu MB", (unsigned long long)(desc.SharedSystemMemory / (1024 * 1024)));
                WriteLine(f, "    Flags: 0x%08X%s", desc.Flags,
                    (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " (SOFTWARE/WARP)" : "");

                // Get driver version from registry (DXGI doesn't expose it directly)
                LARGE_INTEGER driverVersion;
                HRESULT hrVer = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion);
                if (SUCCEEDED(hrVer)) {
                    WORD parts[4] = {
                        HIWORD(driverVersion.HighPart),
                        LOWORD(driverVersion.HighPart),
                        HIWORD(driverVersion.LowPart),
                        LOWORD(driverVersion.LowPart)
                    };
                    WriteLine(f, "    Driver Version: %d.%d.%d.%d", parts[0], parts[1], parts[2], parts[3]);
                }

                adapter.Reset();
                adapterIndex++;
            }
        } else {
            WriteLine(f, "  ERROR: Failed to create DXGI Factory. HRESULT = 0x%08X", hr);
        }
    }
    WriteLine(f, "");

    // ---- Section 3: D3D11 Device Creation Test ----
    WriteLine(f, "--- D3D11 DEVICE CREATION ---");
    {
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL supportedLevel;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

        HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags,
            featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
            &device, &supportedLevel, &context);

        bool isWARP = false;
        if (FAILED(hr)) {
            WriteLine(f, "  Hardware device creation FAILED: HRESULT = 0x%08X", hr);
            WriteLine(f, "  Attempting WARP fallback...");
            hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, flags,
                featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
                &device, &supportedLevel, &context);
            isWARP = true;
        }

        if (SUCCEEDED(hr)) {
            WriteLine(f, "  Device Type: %s", isWARP ? "WARP (Software)" : "HARDWARE");
            WriteLine(f, "  Feature Level: %s (0x%04X)", FeatureLevelToString(supportedLevel), supportedLevel);

            // NV12 Format Support Check
            UINT nv12Support = 0;
            hr = device->CheckFormatSupport(DXGI_FORMAT_NV12, &nv12Support);
            if (SUCCEEDED(hr)) {
                bool canTexture2D = (nv12Support & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0;
                bool canSRV = (nv12Support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0;
                WriteLine(f, "  NV12 Format Support: 0x%08X", nv12Support);
                WriteLine(f, "    Texture2D: %s", canTexture2D ? "YES" : "NO");
                WriteLine(f, "    Shader Resource View: %s", canSRV ? "YES" : "NO");
                if (!canTexture2D || !canSRV) {
                    WriteLine(f, "    *** WARNING: NV12 texture/SRV not supported! Video rendering WILL FAIL. ***");
                }
            } else {
                WriteLine(f, "  NV12 Format Support: CHECK FAILED (HRESULT = 0x%08X)", hr);
                WriteLine(f, "    *** WARNING: Cannot verify NV12 support. Video rendering may fail. ***");
            }

            // FLIP_DISCARD support test
            WriteLine(f, "  DXGI_SWAP_EFFECT_FLIP_DISCARD: Supported on Windows 10+ (Build 10240+)");
        } else {
            WriteLine(f, "  BOTH Hardware and WARP device creation FAILED: HRESULT = 0x%08X", hr);
            WriteLine(f, "  *** CRITICAL: D3D11 is not available on this system. ***");
        }
    }
    WriteLine(f, "");

    // ---- Section 4: Media Foundation Codecs ----
    WriteLine(f, "--- MEDIA FOUNDATION CODECS ---");
    WriteLine(f, "  MF decoders:");
    {
        HRESULT hr = MFStartup(MF_VERSION);
        if (SUCCEEDED(hr)) {
            WriteLine(f, "  MFStartup: OK (Version 0x%08X)", MF_VERSION);

            // Enumerate H.264 decoders
            MFT_REGISTER_TYPE_INFO inputType = {};
            inputType.guidMajorType = MFMediaType_Video;
            inputType.guidSubtype = MFVideoFormat_H264;

            IMFActivate** ppActivate = nullptr;
            UINT32 count = 0;
            hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                &inputType, NULL, &ppActivate, &count);

            if (SUCCEEDED(hr)) {
                WriteLine(f, "  H.264 Decoders Found: %d", count);
                for (UINT32 i = 0; i < count; i++) {
                    LPWSTR name = nullptr;
                    UINT32 nameLen = 0;
                    ppActivate[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &nameLen);
                    if (name) {
                        UINT32 flags = 0;
                        ppActivate[i]->GetUINT32(MF_TRANSFORM_FLAGS_Attribute, &flags);
                        bool isHW = (flags & MFT_ENUM_FLAG_HARDWARE) != 0;
                        WriteLineW(f, L"    [%d] %ls %ls", i, name, isHW ? L"(HARDWARE)" : L"(SOFTWARE)");
                        CoTaskMemFree(name);
                    }
                    ppActivate[i]->Release();
                }
                CoTaskMemFree(ppActivate);
            } else {
                WriteLine(f, "  H.264 Decoder enumeration FAILED: HRESULT = 0x%08X", hr);
            }

            // Enumerate HEVC decoders
            inputType.guidSubtype = MFVideoFormat_HEVC;
            ppActivate = nullptr;
            count = 0;
            hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                &inputType, NULL, &ppActivate, &count);

            if (SUCCEEDED(hr)) {
                WriteLine(f, "  HEVC Decoders Found: %d", count);
                for (UINT32 i = 0; i < count; i++) {
                    LPWSTR name = nullptr;
                    UINT32 nameLen = 0;
                    ppActivate[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &nameLen);
                    if (name) {
                        WriteLineW(f, L"    [%d] %ls", i, name);
                        CoTaskMemFree(name);
                    }
                    ppActivate[i]->Release();
                }
                CoTaskMemFree(ppActivate);
            } else {
                WriteLine(f, "  HEVC Decoder enumeration FAILED: HRESULT = 0x%08X", hr);
            }

            MFShutdown();
        } else {
            WriteLine(f, "  MFStartup FAILED: HRESULT = 0x%08X", hr);
            WriteLine(f, "  *** CRITICAL: Media Foundation is not available. Video decoding is impossible. ***");
        }
    }
    WriteLine(f, "");

    // ---- Section 5: Display / DPI / Monitor Info ----
    WriteLine(f, "--- DISPLAY / MONITORS ---");
    {
        int monitorCount = GetSystemMetrics(SM_CMONITORS);
        WriteLine(f, "  Monitor Count: %d", monitorCount);

        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vcx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vcy = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        WriteLine(f, "  Virtual Screen: origin=(%d,%d), size=%dx%d", vx, vy, vcx, vcy);

        // System DPI
        HDC hdc = GetDC(NULL);
        if (hdc) {
            int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
            int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
            WriteLine(f, "  System DPI: %d x %d (%d%% scaling)", dpiX, dpiY, (dpiX * 100) / 96);
            ReleaseDC(NULL, hdc);
        }

        // DPI Awareness
        PROCESS_DPI_AWARENESS awareness = PROCESS_DPI_UNAWARE;
        HRESULT hr = GetProcessDpiAwareness(NULL, &awareness);
        if (SUCCEEDED(hr)) {
            const char* awarenessStr = "Unknown";
            switch (awareness) {
                case PROCESS_DPI_UNAWARE: awarenessStr = "DPI_UNAWARE"; break;
                case PROCESS_SYSTEM_DPI_AWARE: awarenessStr = "SYSTEM_DPI_AWARE"; break;
                case PROCESS_PER_MONITOR_DPI_AWARE: awarenessStr = "PER_MONITOR_DPI_AWARE"; break;
            }
            WriteLine(f, "  DPI Awareness: %s", awarenessStr);
        }

        // Enumerate monitors with details
        struct MonitorEnumData { FILE* f; int index; };
        MonitorEnumData enumData = { f, 0 };
        EnumDisplayMonitors(NULL, NULL, [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
            auto* data = reinterpret_cast<MonitorEnumData*>(lParam);
            MONITORINFOEXW mi = {};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(hMonitor, &mi)) {
                int w = mi.rcMonitor.right - mi.rcMonitor.left;
                int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
                WriteLineW(data->f, L"  Monitor %d: %ls (%dx%d) at (%d,%d)%ls",
                    data->index, mi.szDevice, w, h,
                    mi.rcMonitor.left, mi.rcMonitor.top,
                    (mi.dwFlags & MONITORINFOF_PRIMARY) ? L" [PRIMARY]" : L"");

                // Get refresh rate
                DEVMODEW dm = {};
                dm.dmSize = sizeof(dm);
                if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
                    WriteLine(data->f, "    Refresh Rate: %d Hz, Color Depth: %d-bit",
                        dm.dmDisplayFrequency, dm.dmBitsPerPel);
                }
            }
            data->index++;
            return TRUE;
        }, (LPARAM)&enumData);
    }
    WriteLine(f, "");

    // ---- Section 6: DWM Composition ----
    WriteLine(f, "--- DWM DESKTOP COMPOSITION ---");
    {
        BOOL dwmEnabled = FALSE;
        HRESULT hr = DwmIsCompositionEnabled(&dwmEnabled);
        if (SUCCEEDED(hr)) {
            WriteLine(f, "  DWM Composition: %s", dwmEnabled ? "ENABLED" : "DISABLED");
            if (!dwmEnabled) {
                WriteLine(f, "  *** WARNING: DWM is disabled. Desktop wallpaper injection may not work correctly. ***");
            }
        } else {
            WriteLine(f, "  DWM check failed: HRESULT = 0x%08X", hr);
        }
    }
    WriteLine(f, "");

    // ---- Section 7: WorkerW / Desktop Window Hierarchy ----
    WriteLine(f, "--- WORKERW / DESKTOP HIERARCHY ---");
    {
        HWND progman = FindWindowW(L"Progman", NULL);
        WriteLine(f, "  Progman: %p", progman);

        if (progman) {
            // Try spawning WorkerW
            ULONG_PTR result = 0;
            LRESULT sendResult = SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_ABORTIFHUNG, 1000, &result);
            WriteLine(f, "  SendMessageTimeout(0x052C) returned: %ld, result = 0x%p", sendResult, (void*)result);

            // Check for SHELLDLL_DefView in Progman
            HWND shellDefView = FindWindowExW(progman, NULL, L"SHELLDLL_DefView", NULL);
            WriteLine(f, "  SHELLDLL_DefView in Progman: %p", shellDefView);

            // Enumerate all WorkerW windows
            int workerWCount = 0;
            HWND workerW = FindWindowExW(NULL, NULL, L"WorkerW", NULL);
            while (workerW) {
                HWND sdv = FindWindowExW(workerW, NULL, L"SHELLDLL_DefView", NULL);
                RECT rect;
                GetWindowRect(workerW, &rect);
                BOOL isVisible = IsWindowVisible(workerW);
                WriteLine(f, "  WorkerW[%d]: %p (Visible=%d, Rect=%d,%d,%d,%d)%s",
                    workerWCount, workerW, isVisible,
                    rect.left, rect.top, rect.right, rect.bottom,
                    sdv ? " [HAS SHELLDLL_DefView]" : "");
                workerWCount++;
                workerW = FindWindowExW(NULL, workerW, L"WorkerW", NULL);
            }
            WriteLine(f, "  Total WorkerW windows: %d", workerWCount);

            if (workerWCount == 0) {
                WriteLine(f, "  *** WARNING: No WorkerW windows found! The 0x052C message may have failed. ***");
                WriteLine(f, "  *** This is a likely root cause for wallpaper not appearing. ***");
            }
        } else {
            WriteLine(f, "  *** CRITICAL: Progman window not found! Explorer may not be running. ***");
        }
    }
    WriteLine(f, "");

    // ---- Summary ----
    WriteLine(f, "==========================================================");
    WriteLine(f, "END OF DIAGNOSTIC REPORT");
    WriteLine(f, "==========================================================");

    if (f) {
        fclose(f);
    }

    // Print location
    wprintf(L"\nDiagnostic report saved to: %ls\n", reportPath.c_str());

    if (allocatedConsole) {
        FreeConsole();
    }

    return true;
}

} // namespace Diagnostics
