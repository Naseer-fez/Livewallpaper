#include "synchronization_manager.h"

SynchronizationManager::SynchronizationManager() {
    m_hWakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    m_hDetachedEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
}

SynchronizationManager::~SynchronizationManager() {
    Clear();
    if (m_hWakeEvent) {
        CloseHandle(m_hWakeEvent);
        m_hWakeEvent = nullptr;
    }
    if (m_hDetachedEvent) {
        CloseHandle(m_hDetachedEvent);
        m_hDetachedEvent = nullptr;
    }
}

void SynchronizationManager::SetRunning(bool running) {
    m_runThread.store(running, std::memory_order_release);
    Wake();
}

bool SynchronizationManager::IsRunning() const {
    return m_runThread.load(std::memory_order_acquire);
}

void SynchronizationManager::SetPaused(bool paused) {
    m_isPaused.store(paused, std::memory_order_release);
}

bool SynchronizationManager::IsPaused() const {
    return m_isPaused.load(std::memory_order_acquire);
}

void SynchronizationManager::SetThrottled(bool throttled) {
    m_isThrottled.store(throttled, std::memory_order_release);
}

bool SynchronizationManager::IsThrottled() const {
    return m_isThrottled.load(std::memory_order_acquire);
}

void SynchronizationManager::SetFPSLimit(int fps) {
    m_fpsLimit.store(fps, std::memory_order_release);
}

int SynchronizationManager::GetFPSLimit() const {
    return m_fpsLimit.load(std::memory_order_acquire);
}

void SynchronizationManager::RequestResize(int width, int height) {
    uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(width)) << 32) | static_cast<uint32_t>(height);
    if (m_newDimensions.load(std::memory_order_acquire) == packed &&
        m_resizeRequested.load(std::memory_order_acquire)) {
        return;
    }
    m_newDimensions.store(packed, std::memory_order_release);
    m_resizeRequested.store(true, std::memory_order_release);
    Wake();
}

bool SynchronizationManager::CheckResize(int& outWidth, int& outHeight) {
    bool expected = true;
    if (m_resizeRequested.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        uint64_t packed = m_newDimensions.load(std::memory_order_acquire);
        outWidth = static_cast<int>(packed >> 32);
        outHeight = static_cast<int>(packed & 0xFFFFFFFF);
        return true;
    }
    return false;
}

void SynchronizationManager::RequestRecreate(HWND hWnd) {
    m_newHWnd.store(hWnd, std::memory_order_release);
    m_recreateRequested.store(true, std::memory_order_release);
    Wake();
}

bool SynchronizationManager::CheckRecreate(HWND& outHWnd) {
    bool expected = true;
    if (m_recreateRequested.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        outHWnd = m_newHWnd.load(std::memory_order_acquire);
        return true;
    }
    return false;
}

void SynchronizationManager::SetDetached(bool detached) {
    m_isDetached.store(detached, std::memory_order_release);
}

bool SynchronizationManager::IsDetached() const {
    return m_isDetached.load(std::memory_order_acquire);
}

void SynchronizationManager::RequestChangeVideo(const std::wstring& path) {
    auto msg = std::make_unique<PathMessage>();
    msg->path = path;
    m_pathQueue.Push(std::move(msg));
    Wake();
}

bool SynchronizationManager::PopVideoChange(std::wstring& outPath) {
    std::unique_ptr<PathMessage> msg;
    if (m_pathQueue.Pop(msg)) {
        if (msg) {
            outPath = msg->path;
            return true;
        }
    }
    return false;
}

void SynchronizationManager::Clear() {
    m_pathQueue.Clear();
}

void SynchronizationManager::Wake() {
    if (m_hWakeEvent) {
        SetEvent(m_hWakeEvent);
    }
}

HANDLE SynchronizationManager::GetWakeEvent() const {
    return m_hWakeEvent;
}

void SynchronizationManager::SignalDetached() {
    if (m_hDetachedEvent) {
        SetEvent(m_hDetachedEvent);
    }
}

bool SynchronizationManager::WaitForDetached(DWORD timeoutMs) {
    if (!m_hDetachedEvent) return true;
    return (WaitForSingleObject(m_hDetachedEvent, timeoutMs) == WAIT_OBJECT_0);
}
