#pragma once

#include "Foundation.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace vmex::recording
{
    class AudioMirror final
    {
    public:
        AudioMirror() = default;
        ~AudioMirror();
        AudioMirror(const AudioMirror&) = delete;
        AudioMirror& operator=(const AudioMirror&) = delete;
        Status Start(std::wstring_view sourceDevice, std::wstring_view targetDevice);
        void Stop() noexcept;
        [[nodiscard]] Status State() const;

    private:
        std::thread m_thread;
        std::atomic_bool m_stop{false};
        mutable std::mutex m_mutex;
        Status m_result;
    };
}
