#include "RecordingPanelState.h"

#include "Platform.h"
#include <windows.h>
#include <algorithm>
#include <iterator>

namespace vmex::recording
{
    namespace
    {
        struct SharedState final
        {
            DWORD signature = 0;
            DWORD available = 0;
            DWORD enabled = 0;
            DWORD cleanupPending = 0;
            wchar_t monitor[1024]{};
        };

        constexpr DWORD kSignature = 0x56525031;

        std::wstring CacheName(std::wstring_view name)
        {
            return name.empty() ? L"Local\\VmExt.RecordingPanel.S" + std::to_wstring(platform::GetCurrentSessionId()) : std::wstring(name);
        }

        struct Handle final
        {
            HANDLE value = nullptr;
            ~Handle() { if (value) ::CloseHandle(value); }
        };

        struct Unlock final
        {
            HANDLE value;
            ~Unlock() { ::ReleaseMutex(value); }
        };
    }

    PanelStateCache::PanelStateCache(std::wstring name) : m_name(CacheName(name)) {}
    PanelStateCache::~PanelStateCache() { Stop(); }

    Status PanelStateCache::Start()
    {
        if (m_view) return Status::Ok();
        m_mutex = ::CreateMutexW(nullptr, FALSE, (m_name + L".Lock").c_str());
        m_mapping = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedState), m_name.c_str());
        if (m_mapping) m_view = ::MapViewOfFile(static_cast<HANDLE>(m_mapping), FILE_MAP_WRITE, 0, 0, sizeof(SharedState));
        if (m_mutex && m_view) return Status::Ok();
        const auto result = Status::FromLastError(L"recording-panel-cache");
        Stop();
        return result;
    }

    void PanelStateCache::Stop()
    {
        if (m_view) ::UnmapViewOfFile(m_view);
        if (m_mapping) ::CloseHandle(static_cast<HANDLE>(m_mapping));
        if (m_mutex) ::CloseHandle(static_cast<HANDLE>(m_mutex));
        m_view = m_mapping = m_mutex = nullptr;
    }

    void PanelStateCache::Publish(const PanelState& state)
    {
        if (!m_view || state.recording.monitorDevice.size() >= std::size(SharedState{}.monitor)) return;
        const DWORD wait = ::WaitForSingleObject(static_cast<HANDLE>(m_mutex), INFINITE);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return;
        Unlock unlock{ static_cast<HANDLE>(m_mutex) };
        SharedState value;
        value.available = state.available;
        value.enabled = state.recording.enabled;
        value.cleanupPending = state.recording.cleanupPending;
        std::copy(state.recording.monitorDevice.begin(), state.recording.monitorDevice.end(), value.monitor);
        value.signature = kSignature;
        *static_cast<SharedState*>(m_view) = value;
    }

    bool PanelStateCache::Read(PanelState& state, std::wstring_view name)
    {
        const auto key = CacheName(name);
        Handle mutex{ ::OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (key + L".Lock").c_str()) };
        if (!mutex.value) return false;
        const DWORD wait = ::WaitForSingleObject(mutex.value, 0);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return false;
        Unlock unlock{ mutex.value };
        Handle mapping{ ::OpenFileMappingW(FILE_MAP_READ, FALSE, key.c_str()) };
        if (!mapping.value) return false;
        const auto view = ::MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, sizeof(SharedState));
        if (!view) return false;
        const auto value = *static_cast<const SharedState*>(view);
        ::UnmapViewOfFile(view);
        if (value.signature != kSignature || value.available > 1 || value.enabled > 1 || value.cleanupPending > 1 || value.monitor[1023]) return false;
        state.available = value.available != 0;
        state.recording = { true, value.enabled != 0, value.cleanupPending != 0, value.monitor };
        return true;
    }
}
