#pragma once

#include "Foundation.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace vmex::recording
{
    class IEndpointLockActions
    {
    public:
        virtual ~IEndpointLockActions() = default;
        virtual Status ReadVolume(float& level) = 0;
        virtual Status SetVolume(float level) = 0;
        virtual Status ReadDefault(unsigned role, std::wstring& endpoint) = 0;
        virtual Status SetDefault(unsigned role, std::wstring_view endpoint) = 0;
    };

    Status EnforceRecordingEndpoint(IEndpointLockActions& actions, std::wstring_view endpoint);

    class RecordingEndpointLock final
    {
    public:
        ~RecordingEndpointLock();
        Status Start(std::wstring_view endpoint);
        void Stop() noexcept;
        [[nodiscard]] Status State() const;

    private:
        struct Signal;
        std::shared_ptr<Signal> m_signal;
        std::thread m_thread;
        std::atomic_bool m_stop{false};
        mutable std::mutex m_mutex;
        Status m_result;
    };
}
