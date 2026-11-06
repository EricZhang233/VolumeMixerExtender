#pragma once

#include "AudioTypes.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

namespace vmex::presentation
{
    inline constexpr std::uint64_t kDeviceSelectionDelayMs = 32;

    using MeterClock = std::chrono::steady_clock;
    inline constexpr auto kMeterFrameInterval = std::chrono::nanoseconds(16'666'667);
    inline constexpr auto kMeterSampleInterval = 2 * kMeterFrameInterval;

    struct MeterFrame final
    {
        MeterClock::time_point now;
        MeterClock::duration elapsed;
        bool sample;
    };

    class MeterCadence final
    {
    public:
        std::optional<MeterFrame> Tick(MeterClock::time_point now)
        {
            constexpr auto tolerance = std::chrono::milliseconds(1);
            if (m_lastFrame && now + tolerance < m_nextFrame) return {};
            const auto elapsed = m_lastFrame ? now - *m_lastFrame : kMeterFrameInterval;
            if (!m_lastFrame) m_nextFrame = m_nextSample = now;
            m_lastFrame = now;
            Advance(m_nextFrame, now, kMeterFrameInterval);
            const bool sample = now + tolerance >= m_nextSample;
            if (sample) Advance(m_nextSample, now, kMeterSampleInterval);
            return MeterFrame{ now, elapsed, sample };
        }

    private:
        static void Advance(MeterClock::time_point& deadline, MeterClock::time_point now, MeterClock::duration interval)
        {
            deadline += interval;
            if (deadline <= now) deadline = now + interval;
        }

        std::optional<MeterClock::time_point> m_lastFrame;
        MeterClock::time_point m_nextFrame;
        MeterClock::time_point m_nextSample;
    };

    class MeterLevel final
    {
    public:
        void Sample(float peak)
        {
            constexpr float deadband = 0.008f;
            const float floor = m_target > 0.0f ? 0.004f : 0.008f;
            const float next = std::isfinite(peak) && peak > floor ? std::min(peak, 1.0f) : 0.0f;
            if (next == 0.0f || next == 1.0f || m_target == 0.0f || std::abs(next - m_target) >= deadband)
                m_target = next;
        }

        float Advance(MeterClock::duration elapsed)
        {
            if (m_level == m_target) return m_level;
            const float seconds = std::chrono::duration<float>(elapsed).count();
            const float timeConstant = m_target > m_level ? 0.025f : 0.14f;
            m_level += (m_target - m_level) * -std::expm1(-seconds / timeConstant);
            if (std::abs(m_target - m_level) <= 0.001f) m_level = m_target;
            return m_level;
        }

    private:
        float m_target = 0.0f;
        float m_level = 0.0f;
    };

    template<class T>
    class PendingSelection final
    {
    public:
        void Queue(T value, std::uint64_t now, std::uint64_t delay = 200)
        {
            m_value = std::move(value);
            m_due = now + delay;
        }
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

    inline bool SameDeviceControl(const audio::DeviceInfo& left, const audio::DeviceInfo& right)
    {
        return left.id == right.id && left.friendlyName == right.friendlyName &&
            left.description == right.description && left.state == right.state &&
            left.recommendedIcon == right.recommendedIcon;
    }

    inline bool SameDeviceRows(const std::vector<audio::DeviceInfo>& left, const std::vector<audio::DeviceInfo>& right)
    {
        return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](const auto& a, const auto& b)
            { return SameDeviceControl(a, b) && a.isDefault == b.isDefault; });
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
        bool showDriverName = true;
    };

    inline void SelectVisibleDefaults(DeviceSnapshot& snapshot, const DeviceSnapshot& previous, std::wstring_view monitor)
    {
        const auto select = [](std::vector<audio::DeviceInfo>& devices, std::wstring_view preferred)
        {
            PrioritizeMonitor(devices, preferred);
            return !devices.empty() && devices.front().isDefault ? devices.front().id : std::wstring{};
        };
        const auto current = [](const std::vector<audio::DeviceInfo>& devices, std::wstring_view fallback)
        {
            const auto found = std::find_if(devices.begin(), devices.end(), [](const auto& device) { return device.isDefault; });
            return found != devices.end() ? std::wstring_view(found->id) : fallback;
        };
        snapshot.defaultRender = select(snapshot.render,
            snapshot.recordingMode ? monitor : current(snapshot.render, previous.defaultRender));
        snapshot.defaultCapture = select(snapshot.capture, current(snapshot.capture, previous.defaultCapture));
    }

    inline bool SameVolumeRows(const DeviceSnapshot& left, const DeviceSnapshot& right)
    {
        return left.showDriverName == right.showDriverName && SameDeviceRows(left.render, right.render) &&
            SameDeviceRows(left.capture, right.capture);
    }

    inline bool CanReuseVolumeRows(const DeviceSnapshot& left, const DeviceSnapshot& right)
    {
        return left.showDriverName == right.showDriverName &&
            std::is_permutation(left.render.begin(), left.render.end(), right.render.begin(), right.render.end(), SameDeviceControl) &&
            std::is_permutation(left.capture.begin(), left.capture.end(), right.capture.begin(), right.capture.end(), SameDeviceControl);
    }

    inline bool SameDeviceSnapshot(const DeviceSnapshot& left, const DeviceSnapshot& right)
    {
        return left.recordingMode == right.recordingMode && SameVolumeRows(left, right) &&
            std::equal(left.routingRender.begin(), left.routingRender.end(),
                right.routingRender.begin(), right.routingRender.end(), SameDeviceControl) &&
            left.defaultRender == right.defaultRender && left.defaultCapture == right.defaultCapture &&
            left.appsEndpoint == right.appsEndpoint;
    }
}
