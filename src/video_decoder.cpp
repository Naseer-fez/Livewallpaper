#include "video_decoder.h"
#include "utils.h"
#include "timer.h"
#include <chrono>

VideoDecoder::VideoDecoder() {}

VideoDecoder::~VideoDecoder() {
    Shutdown();
}

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

void VideoDecoder::Shutdown() {
    CloseVideo();
    m_pDeviceManager.Reset();
    m_pDevice = nullptr;
    MFShutdown();
    LOG_INFO("VideoDecoder shut down successfully.");
}

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

    m_decodedFrameCount = 0;
    m_renderedFrameCount = 0;
    m_decodeStartTime = GetTickCount64();
    m_decodeStallWarned = false;

    m_runThread = true;
    m_videoLoaded = true;
    m_decodeThread = std::thread(&VideoDecoder::DecodingThreadProc, this);

    LOG_INFO("LoadVideo: Decoding thread successfully spawned.");
    return true;
}

void VideoDecoder::CloseVideo() {
    m_videoLoaded = false;
    m_runThread = false;

    // Flush any pending synchronous ReadSample calls to prevent joining threads from hanging
    if (m_pSourceReader) {
        m_pSourceReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    }

    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }

    m_sampleQueue.Clear();

    m_pSourceReader.Reset();
    m_pVideoSRV_Y.Reset();
    m_pVideoSRV_UV.Reset();
    m_pVideoTexture.Reset();
    m_pActiveSRV_Y.Reset();
    m_pActiveSRV_UV.Reset();
    m_videoWidth = 0;
    m_videoHeight = 0;
    m_videoTextureWidth = 0;
    m_videoTextureHeight = 0;
}

void VideoDecoder::SetPaused(bool paused) {
    if (m_isPaused.load() && !paused) {
        // Transition from paused to resumed: reset the playback timer
        // to prevent large elapsed time jumps.
        m_playbackTimer.Reset();
    }
    m_isPaused.store(paused);
}

bool VideoDecoder::ReallocateVideoTexture(int width, int height) {
    m_pVideoSRV_Y.Reset();
    m_pVideoSRV_UV.Reset();
    m_pVideoTexture.Reset();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    HRESULT hr = m_pDevice->CreateTexture2D(&desc, nullptr, &m_pVideoTexture);
    if (FAILED(hr)) {
        LOG_ERROR("ReallocateVideoTexture CreateTexture2D failed. HRESULT: 0x%08X", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDescY = {};
    srvDescY.Format = DXGI_FORMAT_R8_UNORM;
    srvDescY.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDescY.Texture2D.MipLevels = 1;
    srvDescY.Texture2D.MostDetailedMip = 0;

    hr = m_pDevice->CreateShaderResourceView(m_pVideoTexture.Get(), &srvDescY, &m_pVideoSRV_Y);
    if (FAILED(hr)) {
        LOG_ERROR("ReallocateVideoTexture CreateShaderResourceView Y failed. HRESULT: 0x%08X", hr);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDescUV = {};
    srvDescUV.Format = DXGI_FORMAT_R8G8_UNORM;
    srvDescUV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDescUV.Texture2D.MipLevels = 1;
    srvDescUV.Texture2D.MostDetailedMip = 0;

    hr = m_pDevice->CreateShaderResourceView(m_pVideoTexture.Get(), &srvDescUV, &m_pVideoSRV_UV);
    if (FAILED(hr)) {
        LOG_ERROR("ReallocateVideoTexture CreateShaderResourceView UV failed. HRESULT: 0x%08X", hr);
        return false;
    }

    m_videoTextureWidth = width;
    m_videoTextureHeight = height;

    LOG_INFO("Reallocated local video texture to match hardware/software frame size: %dx%d", width, height);
    return true;
}

void VideoDecoder::DecodingThreadProc() {
    LOG_INFO("VideoDecoder background thread started.");

    HRESULT hrCOM = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hrCOM)) {
        LOG_ERROR("CoInitializeEx failed in background decoding thread. HRESULT: 0x%08X", hrCOM);
    }

    while (m_runThread) {
        if (m_isPaused.load()) {
            Timer::PreciseSleep(100.0);
            continue;
        }

        // Stall detection: warn if no frames decoded after 5 seconds
        if (m_decodedFrameCount == 0 && (GetTickCount64() - m_decodeStartTime) > 5000 && !m_decodeStallWarned) {
            LOG_ERROR("CRITICAL: No frames decoded after 5 seconds. Decoding may have stalled on this machine.");
            m_decodeStallWarned = true;
        }

        // Wait if the queue is full to avoid decoding too far ahead
        while (m_runThread && !m_isPaused.load()) {
            if (m_sampleQueue.Size() < 5) { // Maximum of 5 frames buffered
                break;
            }
            Timer::PreciseSleep(5.0);
        }

        if (!m_runThread) {
            break;
        }

        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        Microsoft::WRL::ComPtr<IMFSample> pSample;

        // Read next sample from Media Foundation Source Reader
        HRESULT hr = m_pSourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &pSample
        );

        if (FAILED(hr)) {
            LOG_ERROR("ReadSample failed. HRESULT: 0x%08X", hr);
            Timer::PreciseSleep(10.0);
            continue;
        }

        // Loop playbacks automatically when we reach the end of stream
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            LOG_INFO("Reached end of video stream. Looping...");
            
            // Flush decoder pipeline to release DXVA2 buffers and prevent VRAM accumulation
            m_pSourceReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_I8;
            var.hVal.QuadPart = 0;
            
            hr = m_pSourceReader->SetCurrentPosition(GUID_NULL, var);
            PropVariantClear(&var);

            if (FAILED(hr)) {
                LOG_ERROR("SetCurrentPosition(0) failed. HRESULT: 0x%08X", hr);
            }
            continue;
        }

        if (pSample) {
            m_decodedFrameCount++;
            if (m_decodedFrameCount % 100 == 0) {
                LOG_INFO("VideoDecoder: Decoded %d frames so far", m_decodedFrameCount);
            }
            IMFSample* pRawSample = pSample.Detach();
            if (!m_sampleQueue.Push(pRawSample)) {
                // If queue push fails, release the sample to prevent leak
                pRawSample->Release();
            }
        } else {
            // Null sample without end-of-stream is suspicious
            LOG_WARN("VideoDecoder: ReadSample returned null sample without EOS. Flags = 0x%08X", flags);
            // Sleep briefly when no sample is fetched but no end-of-stream reached yet
            Timer::PreciseSleep(2.0);
        }
    }

    LOG_INFO("VideoDecoder background thread stopped.");
    if (SUCCEEDED(hrCOM)) {
        CoUninitialize();
    }
}

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
            m_renderedFrameCount++;
            if (m_renderedFrameCount == 1) {
                LOG_INFO("MILESTONE: First video frame successfully uploaded to GPU texture.");
            }
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
            m_renderedFrameCount++;
            if (m_renderedFrameCount == 1) {
                LOG_INFO("MILESTONE: First video frame successfully uploaded to GPU texture.");
            }
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
        m_renderedFrameCount++;
        if (m_renderedFrameCount == 1) {
            LOG_INFO("MILESTONE: First video frame successfully uploaded to GPU texture.");
        }
        return true;
    }

    LOG_ERROR("UpdateFrame: All frame extraction paths failed to extract media buffer.");
    return false;
}
