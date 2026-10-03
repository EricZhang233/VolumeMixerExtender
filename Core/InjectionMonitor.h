#pragma once

#include "Foundation.h"
#include "InjectionService.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace vmex::inject
{
    class InjectionMonitor final
    {
    public:
        InjectionMonitor(IInjectionService& service, Options options, unsigned long pollIntervalMs);
        ~InjectionMonitor();

        InjectionMonitor(const InjectionMonitor&) = delete;
        InjectionMonitor& operator=(const InjectionMonitor&) = delete;
        InjectionMonitor(InjectionMonitor&&) = delete;
        InjectionMonitor& operator=(InjectionMonitor&&) = delete;

        void Start();
        void Stop();

        [[nodiscard]] bool IsRunning() const noexcept;
        [[nodiscard]] State Snapshot() const;
        [[nodiscard]] Status Snapshot(std::wstring& detail) const;

    private:
        void Loop();
        void Record(const State& state, const Status& status);

        IInjectionService& m_service;
        Options m_options;
        unsigned long m_pollIntervalMs;
        std::atomic<bool> m_running{ false };
        std::atomic<bool> m_stopRequested{ false };
        void* m_stopEvent = nullptr;
        std::thread m_thread;

        mutable std::mutex m_mutex;
        State m_state;
        Status m_last = Status::Ok();
    };
}
