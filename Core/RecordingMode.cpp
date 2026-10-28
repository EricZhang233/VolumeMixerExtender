#include "RecordingMode.h"

#include "Logger.h"

namespace vmex::recording
{
    namespace
    {
        template<class Action>
        Status Guard(Action&& action)
        {
            try { return action(); }
            catch (...) { return Status::Failed(L"recording-backend-exception"); }
        }

        Status Report(std::wstring_view action, Status result)
        {
            log::Logger::Instance().WriteKeyFormat(result ? log::Level::Info : log::Level::Error,
                L"recording", L"log.recording.result", { std::wstring(action), std::to_wstring(result.code), result.detail });
            return result;
        }
    }

    Mode::Mode(IBackend& backend) : m_backend(backend) {}

    Mode::~Mode()
    {
        try { Disable(); } catch (...) {}
        m_backend.StopMirror();
        m_backend.DisconnectVirtualDevice();
    }

    Status Mode::Enable()
    {
        std::lock_guard lock(m_mutex);
        if (m_state.cleanupPending) return Report(L"enable", Status::Failed(L"recording-cleanup-pending"));
        if (m_state.enabled) return Status::Ok();
        std::wstring monitor;
        auto result = Guard([&] { return m_backend.SaveDefaults(monitor); });
        if (!result) return Report(L"enable", result);
        if (monitor.empty()) return Report(L"enable", Status::Failed(L"recording-monitor-missing"));
        result = Guard([&] { return m_backend.ConnectVirtualDevice(); });
        if (!result)
        {
            m_backend.DisconnectVirtualDevice();
            return Report(L"enable", result);
        }
        result = Guard([&] { return m_backend.StartMirror(monitor); });
        if (!result)
        {
            m_backend.StopMirror();
            m_backend.DisconnectVirtualDevice();
            return Report(L"enable", result);
        }
        m_state.monitorDevice = std::move(monitor);
        result = Guard([&] { return m_backend.SelectVirtualDevice(); });
        if (!result)
        {
            const auto restored = Guard([&] { return m_backend.RestoreDefaults(); });
            if (!restored)
            {
                m_state.cleanupPending = true;
                return Report(L"enable", Status::Failed(result.detail + L"; " + restored.detail));
            }
            m_backend.StopMirror();
            m_backend.DisconnectVirtualDevice();
            m_state = {};
            return Report(L"enable", result);
        }
        m_state.enabled = true;
        m_failure = Status::Ok();
        return Report(L"enable", Status::Ok());
    }

    Status Mode::Disable()
    {
        std::lock_guard lock(m_mutex);
        m_failure = Status::Ok();
        if (!m_state.enabled && !m_state.cleanupPending) return Status::Ok();
        const auto result = Guard([&] { return m_backend.RestoreDefaults(); });
        if (!result)
        {
            m_state.cleanupPending = true;
            return Report(L"disable", result);
        }
        m_backend.StopMirror();
        m_backend.DisconnectVirtualDevice();
        m_state = {};
        return Report(L"disable", Status::Ok());
    }

    Status Mode::ChangeMonitor(std::wstring_view monitorDevice)
    {
        std::lock_guard lock(m_mutex);
        if (!m_state.enabled || m_state.cleanupPending)
            return Report(L"monitor", Status::Failed(L"recording-not-ready"));
        if (monitorDevice.empty()) return Status::InvalidArguments(L"monitor-device");
        if (monitorDevice == m_state.monitorDevice) return Status::Ok();
        const auto result = Guard([&] { return m_backend.ChangeMonitor(monitorDevice); });
        if (result) m_state.monitorDevice.assign(monitorDevice);
        return Report(L"monitor", result);
    }

    Status Mode::CheckHealth()
    {
        std::lock_guard lock(m_mutex);
        if (m_state.enabled && !m_state.cleanupPending)
        {
            const auto health = Guard([&] { return m_backend.MirrorState(); });
            if (!health)
            {
                const auto stopped = Disable();
                m_failure = stopped ? health : Status::Failed(health.detail + L"; " + stopped.detail);
                return Report(L"health", m_failure);
            }
        }
        return m_failure;
    }

    Snapshot Mode::State() const { std::lock_guard lock(m_mutex); return m_state; }
}
