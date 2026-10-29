#pragma once

#include "RecordingMode.h"
#include <memory>
#include <array>

namespace vmex::recording
{
    class DefaultOutputMemory final
    {
    public:
        void Remember(std::array<std::wstring, 2> defaults)
        {
            m_initial = std::move(defaults);
            m_monitor = m_initial[1];
            m_active = false;
        }
        void SelectMonitor(std::wstring monitor) { m_monitor = std::move(monitor); m_active = true; }
        const std::wstring& Monitor() const { return m_monitor; }
        const std::array<std::wstring, 2>& Initial() const { return m_initial; }
        std::array<std::wstring, 2> Targets() const
        {
            return m_active ? std::array<std::wstring, 2>{m_monitor, m_monitor} : m_initial;
        }
    private:
        std::array<std::wstring, 2> m_initial;
        std::wstring m_monitor;
        bool m_active = false;
    };

    [[nodiscard]] std::unique_ptr<IBackend> CreateWindowsBackend();
}
