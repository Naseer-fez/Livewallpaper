#include "swap_chain_manager.h"
#include "utils.h"

SwapChainManager::SwapChainManager() {}

SwapChainManager::~SwapChainManager() {
    Shutdown();
}

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

void SwapChainManager::Shutdown() {
    m_renderTargetView.Reset();
    m_swapChain.Reset();
    m_hWnd = nullptr;
    m_width = 0;
    m_height = 0;
    LOG_INFO("SwapChainManager shut down.");
}

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
        LOG_WARN("SwapChainManager: CreateSwapChainForHwnd with FLIP_DISCARD failed. HRESULT = 0x%08X. Falling back to DXGI_SWAP_EFFECT_DISCARD...", hr);

        // Fallback: legacy swap effect with single buffer
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        scd.BufferCount = 1;

        hr = dxgiFactory->CreateSwapChainForHwnd(
            device,
            m_hWnd,
            &scd,
            &fsd,
            NULL,
            &m_swapChain
        );
        LOG_INFO("CreateSwapChain: Fallback CreateSwapChainForHwnd (DISCARD) result = 0x%08X", hr);

        if (FAILED(hr)) {
            LOG_ERROR("SwapChainManager: Fallback CreateSwapChainForHwnd also failed. HRESULT = 0x%08X", hr);
            return false;
        }
    }

    // Verify the created swap chain has valid dimensions
    DXGI_SWAP_CHAIN_DESC1 actualDesc = {};
    HRESULT hrDesc = m_swapChain->GetDesc1(&actualDesc);
    if (SUCCEEDED(hrDesc)) {
        m_bufferCount = actualDesc.BufferCount;
        if (actualDesc.Width == 0 || actualDesc.Height == 0) {
            LOG_WARN("SwapChainManager: Created swap chain has zero-size back buffer! Width=%u, Height=%u", actualDesc.Width, actualDesc.Height);
        } else {
            LOG_INFO("SwapChainManager: Swap chain created successfully. Back buffer: %ux%u, SwapEffect=%d, BufferCount=%u",
                actualDesc.Width, actualDesc.Height, actualDesc.SwapEffect, actualDesc.BufferCount);
        }
    } else {
        LOG_WARN("SwapChainManager: GetDesc1 failed after swap chain creation. HRESULT = 0x%08X", hrDesc);
    }

    return true;
}

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

    // Release RTV before resizing buffers
    if (context) {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->ClearState();
        context->Flush();
    }
    m_renderTargetView.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(
        m_bufferCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0
    );
    LOG_INFO("Resize: SwapChain->ResizeBuffers result = 0x%08X", hr);

    if (FAILED(hr)) {
        LOG_ERROR("SwapChainManager: ResizeBuffers failed. HRESULT = 0x%08X", hr);
        return false;
    }

    return CreateRenderTargetView(device);
}

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
