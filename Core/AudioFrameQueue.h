#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace vmex::recording
{
    class AudioFrameQueue final
    {
    public:
        AudioFrameQueue(std::size_t frames, std::size_t frameBytes)
            : m_data(frames * frameBytes), m_capacity(frames), m_frameBytes(frameBytes) {}

        [[nodiscard]] std::size_t Size() const { return m_size; }

        std::size_t Push(const void* data, std::size_t frames)
        {
            const auto* source = static_cast<const std::byte*>(data);
            std::size_t dropped = 0;
            if (frames > m_capacity)
            {
                dropped = frames - m_capacity;
                if (source) source += dropped * m_frameBytes;
                frames = m_capacity;
            }
            if (m_size + frames > m_capacity)
            {
                const auto discard = m_size + frames - m_capacity;
                m_head = (m_head + discard) % m_capacity;
                m_size -= discard;
                dropped += discard;
            }
            for (std::size_t i = 0; i < frames; ++i)
            {
                auto* target = m_data.data() + ((m_head + m_size + i) % m_capacity) * m_frameBytes;
                if (source) std::memcpy(target, source + i * m_frameBytes, m_frameBytes);
                else std::memset(target, 0, m_frameBytes);
            }
            m_size += frames;
            return dropped;
        }

        std::size_t Pop(void* data, std::size_t frames)
        {
            auto* target = static_cast<std::byte*>(data);
            const auto count = std::min(m_size, frames);
            for (std::size_t i = 0; i < count; ++i)
                std::memcpy(target + i * m_frameBytes,
                    m_data.data() + ((m_head + i) % m_capacity) * m_frameBytes, m_frameBytes);
            if (frames > count) std::memset(target + count * m_frameBytes, 0, (frames - count) * m_frameBytes);
            if (m_capacity) m_head = (m_head + count) % m_capacity;
            m_size -= count;
            return count;
        }

    private:
        std::vector<std::byte> m_data;
        std::size_t m_capacity;
        std::size_t m_frameBytes;
        std::size_t m_head = 0;
        std::size_t m_size = 0;
    };
}
