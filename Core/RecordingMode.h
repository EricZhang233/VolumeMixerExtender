#pragma once

#include "Foundation.h"
#include <mutex>

namespace vmex::recording
{
    struct Snapshot final
    {
        bool known = false;
        bool enabled = false;
        bool cleanupPending = false;
        std::wstring monitorDevice;
    };

    class IBackend
    {
    public:
        virtual ~IBackend() = default;
        virtual Status SaveDefaults(std::wstring& monitorDevice) = 0;
        virtual Status ConnectVirtualDevice() = 0;
        virtual Status StartMirror(std::wstring_view monitorDevice) = 0;
        virtual Status SelectVirtualDevice() = 0;
        virtual Status RestoreDefaults() = 0;
        virtual void StopMirror() noexcept = 0;
        virtual void DisconnectVirtualDevice() noexcept = 0;
        virtual Status ChangeMonitor(std::wstring_view monitorDevice) = 0;
        virtual Status MirrorState() const { return Status::Ok(); }
    };

    class Mode final
    {
    public:
        explicit Mode(IBackend& backend);
        ~Mode();
        Mode(const Mode&) = delete;
        Mode& operator=(const Mode&) = delete;
        Status Enable();
        Status Disable();
        Status ChangeMonitor(std::wstring_view monitorDevice);
        Status CheckHealth();
        [[nodiscard]] Snapshot State() const;

    private:
        IBackend& m_backend;
        Snapshot m_state;
        mutable std::recursive_mutex m_mutex;
        Status m_failure;
    };
}
