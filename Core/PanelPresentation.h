#pragma once

#include "AudioTypes.h"
#include <algorithm>
#include <optional>
#include <utility>

namespace vmex::presentation
{
    template<class T>
    class PendingSelection final
    {
    public:
        void Queue(T value, std::uint64_t now) { m_value = std::move(value); m_due = now + 200; }
        [[nodiscard]] bool Due(std::uint64_t now) const { return m_value && now >= m_due; }
        [[nodiscard]] explicit operator bool() const { return m_value.has_value(); }
        [[nodiscard]] T ValueOr(T value) const { return m_value.value_or(std::move(value)); }
        void Reset() { m_value.reset(); }
        std::optional<T> Take(std::uint64_t now)
        {
            if (!Due(now)) return {};
            return std::exchange(m_value, {});
        }

    private:
        std::optional<T> m_value;
        std::uint64_t m_due = 0;
    };

    inline void PrioritizeMonitor(std::vector<audio::DeviceInfo>& devices, std::wstring_view monitor)
    {
        for (auto& device : devices) device.isDefault = device.id == monitor;
        std::stable_sort(devices.begin(), devices.end(), [](const auto& left, const auto& right)
            { return left.isDefault && !right.isDefault; });
    }

    inline bool SameDeviceRows(const std::vector<audio::DeviceInfo>& left, const std::vector<audio::DeviceInfo>& right)
    {
        return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](const auto& a, const auto& b)
            { return a.id == b.id && a.friendlyName == b.friendlyName && a.description == b.description &&
                a.state == b.state && a.isDefault == b.isDefault; });
    }

    struct DeviceSnapshot
    {
        std::vector<audio::DeviceInfo> render;
        std::vector<audio::DeviceInfo> routingRender;
        std::vector<audio::DeviceInfo> capture;
        std::wstring defaultRender;
        std::wstring defaultCapture;
        std::wstring appsEndpoint;
        bool recordingMode = false;
        bool recordingAvailable = false;
        bool showDriverName = true;
    };

    inline bool SameVolumeRows(const DeviceSnapshot& left, const DeviceSnapshot& right)
    {
        return left.recordingAvailable == right.recordingAvailable &&
            left.showDriverName == right.showDriverName && SameDeviceRows(left.render, right.render) &&
            SameDeviceRows(left.capture, right.capture);
    }

    inline bool SameDeviceSnapshot(const DeviceSnapshot& left, const DeviceSnapshot& right)
    {
        return left.recordingMode == right.recordingMode && SameVolumeRows(left, right) &&
            SameDeviceRows(left.routingRender, right.routingRender) &&
            left.defaultRender == right.defaultRender && left.defaultCapture == right.defaultCapture &&
            left.appsEndpoint == right.appsEndpoint;
    }

    class DeviceSnapshotBuffer final
    {
    public:
        bool Waiting(std::uint64_t now) const { return now < m_due; }
        void Hold(std::uint64_t now, std::uint64_t delay = 400)
        {
            m_due = std::max(m_due, now + delay);
            m_candidate.reset();
        }
        bool Observe(const DeviceSnapshot& snapshot, std::uint64_t now)
        {
            if (now < m_due) return false;
            if (!m_candidate || !SameDeviceSnapshot(*m_candidate, snapshot))
            {
                m_candidate = snapshot;
                m_since = now;
                return false;
            }
            return now - m_since >= 250;
        }
    private:
        std::optional<DeviceSnapshot> m_candidate;
        std::uint64_t m_due = 0;
        std::uint64_t m_since = 0;
    };
}
