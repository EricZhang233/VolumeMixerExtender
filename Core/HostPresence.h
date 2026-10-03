#pragma once

#include "Foundation.h"

namespace vmex::host
{
    class HostPresence final
    {
    public:
        HostPresence() = default;
        ~HostPresence();

        HostPresence(const HostPresence&) = delete;
        HostPresence& operator=(const HostPresence&) = delete;
        HostPresence(HostPresence&&) = delete;
        HostPresence& operator=(HostPresence&&) = delete;

        Status Acquire();
        void Release();

        [[nodiscard]] bool IsHeld() const noexcept;

        [[nodiscard]] static bool IsRunning();
        [[nodiscard]] static std::uint32_t SessionId();
        [[nodiscard]] static std::wstring MutexName();

    private:
        void* m_handle = nullptr;
    };
}
