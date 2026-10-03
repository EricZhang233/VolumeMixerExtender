#include "HostPresence.h"

#include "InjectionContract.h"
#include "Platform.h"

#include <windows.h>

namespace vmex::host
{
    HostPresence::~HostPresence()
    {
        Release();
    }

    std::uint32_t HostPresence::SessionId()
    {
        return platform::GetCurrentSessionId();
    }

    std::wstring HostPresence::MutexName()
    {
        return inject::HostMutexName(SessionId());
    }

    Status HostPresence::Acquire()
    {
        if (m_handle != nullptr)
        {
            return Status::Ok();
        }

        const HANDLE handle = ::CreateMutexW(nullptr, FALSE, MutexName().c_str());
        if (handle == nullptr)
        {
            return Status::FromLastError(L"host-mutex");
        }

        if (::GetLastError() == ERROR_ALREADY_EXISTS)
        {
            ::CloseHandle(handle);
            return Status::Failed(L"host-running");
        }

        m_handle = handle;
        return Status::Ok();
    }

    void HostPresence::Release()
    {
        if (m_handle == nullptr)
        {
            return;
        }

        ::CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }

    bool HostPresence::IsHeld() const noexcept
    {
        return m_handle != nullptr;
    }

    bool HostPresence::IsRunning()
    {
        const HANDLE handle = ::OpenMutexW(SYNCHRONIZE, FALSE, MutexName().c_str());
        if (handle == nullptr)
        {
            return false;
        }

        ::CloseHandle(handle);
        return true;
    }
}
