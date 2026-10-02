#include "Logger.h"

#include <windows.h>

#include <mutex>

namespace vmex::log
{
    struct Logger::Impl final
    {
        std::mutex mutex;
        std::vector<std::shared_ptr<ILogSink>> sinks;
        Level minimumLevel = Level::Info;
        const ITextResolver* resolver = nullptr;
        std::wstring channelPrefix;
        std::uint64_t sequence = 0;
    };

    Logger::Logger()
        : m_impl(std::make_unique<Impl>())
    {
    }

    Logger::~Logger() = default;

    Logger& Logger::Instance()
    {
        static Logger instance;
        return instance;
    }

    void Logger::AddSink(const std::shared_ptr<ILogSink>& sink)
    {
        if (sink == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> guard(m_impl->mutex);
        m_impl->sinks.push_back(sink);
    }

    void Logger::ClearSinks()
    {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        m_impl->sinks.clear();
    }

    std::size_t Logger::SinkCount() const noexcept
    {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        return m_impl->sinks.size();
    }

    void Logger::SetMinimumLevel(Level level)
    {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        m_impl->minimumLevel = level;
    }

    Level Logger::MinimumLevel() const noexcept
    {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        return m_impl->minimumLevel;
    }

    void Logger::SetResolver(const ITextResolver* resolver)
    {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        m_impl->resolver = resolver;
    }

    void Logger::SetChannelPrefix(std::wstring_view prefix)
    {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        m_impl->channelPrefix.assign(prefix);
    }

    void Logger::Write(Level level, std::wstring_view channel, std::wstring_view text)
    {
        Record record;
        std::vector<std::shared_ptr<ILogSink>> sinks;

        {
            std::lock_guard<std::mutex> guard(m_impl->mutex);
            if (level < m_impl->minimumLevel)
            {
                return;
            }

            record.level = level;
            record.time = std::chrono::system_clock::now();
            record.threadId = ::GetCurrentThreadId();
            record.sequence = ++m_impl->sequence;
            record.channel = m_impl->channelPrefix;
            record.channel.append(channel);
            record.text.assign(text);
            sinks = m_impl->sinks;
        }

        for (const auto& sink : sinks)
        {
            if (sink != nullptr)
            {
                sink->Write(record);
            }
        }
    }

    void Logger::WriteKey(Level level, std::wstring_view channel, std::wstring_view key)
    {
        WriteKeyFormat(level, channel, key, {});
    }

    void Logger::WriteKeyFormat(Level level, std::wstring_view channel, std::wstring_view key, const std::vector<std::wstring>& arguments)
    {
        std::wstring text;
        {
            std::lock_guard<std::mutex> guard(m_impl->mutex);
            if (level < m_impl->minimumLevel)
            {
                return;
            }
            if (m_impl->resolver != nullptr)
            {
                text = arguments.empty() ? m_impl->resolver->Resolve(key) : m_impl->resolver->ResolveFormat(key, arguments);
            }
        }

        if (text.empty())
        {
            text.assign(key);
        }

        Write(level, channel, text);
    }

    void Logger::Flush()
    {
        std::lock_guard<std::mutex> guard(m_impl->mutex);
        for (const auto& sink : m_impl->sinks)
        {
            if (sink != nullptr)
            {
                sink->Flush();
            }
        }
    }
}
