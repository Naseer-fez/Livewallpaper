#include "device_manager.h"
#include "utils.h"

extern bool g_forceWARP;

DeviceManager::DeviceManager() {}

DeviceManager::~DeviceManager() {
    Shutdown();
}

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
    D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_HARDWARE;
    if (g_forceWARP) {
        driverType = D3D_DRIVER_TYPE_WARP;
        LOG_WARN("DeviceManager::Initialize: g_forceWARP is true. Forcing WARP driver.");
    }

    LOG_INFO("DeviceManager::Initialize: Attempting D3D11 device creation. Driver Type = %d, Flags = 0x%08X", driverType, creationFlags);

    HRESULT hr = D3D11CreateDevice(
        NULL,
        driverType,
        NULL,
        creationFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_d3dDevice,
        &supportedLevel,
        &m_d3dContext
    );

    LOG_INFO("DeviceManager::Initialize: Device creation outcome: HRESULT = 0x%08X, Supported Feature Level = 0x%04X", hr, supportedLevel);

    if (FAILED(hr) && driverType == D3D_DRIVER_TYPE_HARDWARE) {
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

    // Check NV12 format support (needed for hardware video decoding)
    UINT nv12Support = 0;
    HRESULT hrNV12 = m_d3dDevice->CheckFormatSupport(DXGI_FORMAT_NV12, &nv12Support);
    if (SUCCEEDED(hrNV12)) {
        bool hasTexture2D = (nv12Support & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0;
        bool hasSRV = (nv12Support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0;
        m_supportsNV12 = hasTexture2D && hasSRV;
        LOG_INFO("DeviceManager::Initialize: NV12 format support check: TEXTURE2D=%s, SRV=%s, Overall=%s",
            hasTexture2D ? "YES" : "NO", hasSRV ? "YES" : "NO", m_supportsNV12 ? "SUPPORTED" : "NOT SUPPORTED");
    } else {
        m_supportsNV12 = false;
        LOG_WARN("DeviceManager::Initialize: CheckFormatSupport(NV12) failed. HRESULT = 0x%08X. NV12 will be unavailable.", hrNV12);
    }

    LOG_INFO("DeviceManager successfully initialized.");
    return true;
}

void DeviceManager::Shutdown() {
    ReleaseResources();
    LOG_INFO("DeviceManager shut down.");
}

void DeviceManager::ReleaseResources() {
    if (m_d3dContext) {
        m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
        m_d3dContext->ClearState();
        m_d3dContext->Flush();
    }
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
}
