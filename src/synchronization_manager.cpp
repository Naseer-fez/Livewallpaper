#include "synchronization_manager.h"

SynchronizationManager::SynchronizationManager() {}

SynchronizationManager::~SynchronizationManager() {
    Clear();
}

void SynchronizationManager::SetRunning(bool running) {
    m_runThread.store(running, std::memory_order_release);
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

void SynchronizationManager::SetFPSLimit(int fps) {
    m_fpsLimit.store(fps, std::memory_order_release);
}

int SynchronizationManager::GetFPSLimit() const {
    return m_fpsLimit.load(std::memory_order_acquire);
}

void SynchronizationManager::RequestResize(int width, int height) {
    if (m_newWidth.load(std::memory_order_acquire) == width &&
        m_newHeight.load(std::memory_order_acquire) == height &&
        m_resizeRequested.load(std::memory_order_acquire)) {
        return;
    }
    m_newWidth.store(width, std::memory_order_release);
    m_newHeight.store(height, std::memory_order_release);
    m_resizeRequested.store(true, std::memory_order_release);
}

bool SynchronizationManager::CheckResize(int& outWidth, int& outHeight) {
    bool expected = true;
    if (m_resizeRequested.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        outWidth = m_newWidth.load(std::memory_order_acquire);
        outHeight = m_newHeight.load(std::memory_order_acquire);
        return true;
    }
    return false;
}

void SynchronizationManager::RequestRecreate(HWND hWnd) {
    m_newHWnd.store(hWnd, std::memory_order_release);
    m_recreateRequested.store(true, std::memory_order_release);
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
