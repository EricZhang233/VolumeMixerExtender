#include "InjectionMonitor.h"

#include "Logger.h"

#include <windows.h>

#include <utility>

namespace vmex::inject
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"monitor";
    }

    InjectionMonitor::InjectionMonitor(IInjectionService& service, Options options, unsigned long pollIntervalMs)
        : m_service(service)
        , m_options(std::move(options))
        , m_pollIntervalMs(pollIntervalMs == 0 ? 1000 : pollIntervalMs)
    {
    }

    InjectionMonitor::~InjectionMonitor()
    {
        Stop();

        if (m_stopEvent != nullptr)
        {
            ::CloseHandle(static_cast<HANDLE>(m_stopEvent));
            m_stopEvent = nullptr;
        }
    }

    void InjectionMonitor::Start()
    {
        if (m_running.exchange(true))
        {
            return;
        }

        m_stopRequested.store(false);
        m_stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_thread = std::thread([this]() { Loop(); });

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info, kChannel, L"log.monitor.started", { std::to_wstring(m_pollIntervalMs) });
    }

    void InjectionMonitor::Stop()
    {
        m_stopRequested.store(true);
        if (m_stopEvent != nullptr)
        {
            ::SetEvent(static_cast<HANDLE>(m_stopEvent));
        }

        if (m_thread.joinable())
        {
            m_thread.join();
        }

        if (m_running.exchange(false))
        {
            log::Logger::Instance().WriteKey(log::Level::Info, kChannel, L"log.monitor.stopped");
        }
    }

    bool InjectionMonitor::IsRunning() const noexcept
    {
        return m_running.load();
    }

    State InjectionMonitor::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_state;
    }

    Status InjectionMonitor::Snapshot(std::wstring& detail) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        detail = m_last.detail;
        return m_last;
    }

    void InjectionMonitor::Record(const State& state, const Status& status)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = state;
        m_last = status;
    }

    void InjectionMonitor::Loop()
    {
        while (!m_stopRequested.load())
        {
            State current;
            const auto query = m_service.QueryState(current);

            if (query.IsOk() && current.target.processId != 0 && current.tapLoaded)
            {
                Record(current, Status::Ok());
            }
            else
            {
                State injected;
                const auto status = m_service.Inject(m_options, injected);
                Record(status.IsOk() ? injected : State{}, status);
            }

            if (m_stopEvent != nullptr)
            {
                ::WaitForSingleObject(static_cast<HANDLE>(m_stopEvent), m_pollIntervalMs);
            }
            else
            {
                ::Sleep(m_pollIntervalMs);
            }
        }
    }
}
