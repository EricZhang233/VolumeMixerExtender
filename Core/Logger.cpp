#include "Logger.h"
#include "Strings.h"
#include <windows.h>
#include <atomic>
#include <array>
#include <mutex>
#include <typeinfo>
#include <winrt/base.h>

namespace vmex::log
{
    struct Logger::Impl final
    {
        std::mutex mutex;
        std::vector<std::shared_ptr<ILogSink>> sinks;
        Level minimumLevel = Level::Info;
        std::wstring channelPrefix;
        std::uint64_t sequence = 0;
    };

    Logger::Logger() : m_impl(std::make_unique<Impl>()) {}
    Logger::~Logger() = default;
    Logger& Logger::Instance() { static Logger instance; return instance; }

    void Logger::AddSink(const std::shared_ptr<ILogSink>& sink)
    {
        if (!sink) return;
        std::lock_guard lock(m_impl->mutex);
        m_impl->sinks.push_back(sink);
    }

    void Logger::ClearSinks()
    {
        std::vector<std::shared_ptr<ILogSink>> sinks;
        { std::lock_guard lock(m_impl->mutex); sinks.swap(m_impl->sinks); }
    }

    std::size_t Logger::SinkCount() const noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        return m_impl->sinks.size();
    }

    void Logger::SetMinimumLevel(Level level)
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->minimumLevel = level;
    }

    Level Logger::MinimumLevel() const noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        return m_impl->minimumLevel;
    }

    void Logger::SetChannelPrefix(std::wstring_view prefix)
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->channelPrefix.assign(prefix);
    }

    void Logger::Write(Level level, std::wstring_view channel, std::wstring_view message,
        std::source_location location, std::wstring_view operationId)
    {
        Record record;
        std::vector<std::shared_ptr<ILogSink>> sinks;
        {
            std::lock_guard lock(m_impl->mutex);
            if (level < m_impl->minimumLevel) return;
            record.sequence = ++m_impl->sequence;
            record.channel = m_impl->channelPrefix;
            record.channel.append(channel);
            sinks = m_impl->sinks;
        }
        record.level = level;
        record.channel = Redact(record.channel);
        record.time = std::chrono::system_clock::now();
        record.threadId = ::GetCurrentThreadId();
        record.processId = ::GetCurrentProcessId();
        record.sourceFile = std::filesystem::path(location.file_name()).filename().wstring();
        record.sourceLine = location.line();
        record.function = strings::ToWide(location.function_name());
        record.operationId = Redact(operationId);
        record.text = Redact(message);
        for (const auto& sink : sinks)
            try { if (sink) sink->Write(record); } catch (...) {}
    }

    void Logger::WriteFormat(Level level, std::wstring_view channel, std::wstring_view pattern,
        const std::vector<std::wstring>& arguments, std::source_location location, std::wstring_view operationId)
    {
        if (level < MinimumLevel()) return;
        Write(level, channel, strings::Format(pattern, arguments), location, operationId);
    }

    void Logger::Flush()
    {
        std::vector<std::shared_ptr<ILogSink>> sinks;
        { std::lock_guard lock(m_impl->mutex); sinks = m_impl->sinks; }
        for (const auto& sink : sinks)
            try { if (sink) sink->Flush(); } catch (...) {}
    }

    void Logger::WriteException(std::wstring_view channel, std::wstring_view message,
        std::exception_ptr error, std::source_location location)
    {
        std::wstring detail = L"unknown";
        try { if (error) std::rethrow_exception(error); }
        catch (const winrt::hresult_error& exception)
        {
            detail = L"winrt::hresult_error code=" + std::to_wstring(exception.code().value) + L" message=" + exception.message().c_str();
        }
        catch (const std::exception& exception)
        {
            detail = strings::ToWide(typeid(exception).name()) + L":" + strings::ToWide(exception.what());
        }
        catch (...) {}
        std::array<void*, 32> frames{};
        const auto count = ::CaptureStackBackTrace(1, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
        std::wstring stack;
        for (USHORT i = 0; i < count; ++i)
        {
            wchar_t address[32]{};
            swprintf_s(address, L"0x%p", frames[i]);
            if (!stack.empty()) stack += L";";
            stack += address;
        }
        Write(Level::Error, channel, std::wstring(message) + L" Exception=" + detail + L" CatchStack=" + stack, location);
    }

    OperationTimer::OperationTimer(std::wstring_view channel, std::wstring_view name,
        std::wstring_view operationId, std::source_location location)
        : m_channel(channel), m_name(name), m_id(operationId), m_location(location)
    {
        static std::atomic_uint64_t sequence = 0;
        if (m_id.empty()) m_id = std::to_wstring(::GetCurrentProcessId()) + L"-" +
            std::to_wstring(::GetTickCount64()) + L"-" + std::to_wstring(++sequence);
        Logger::Instance().Write(Level::Info, m_channel, L"OperationStart " + m_name, m_location, m_id);
    }

    OperationTimer::~OperationTimer()
    {
        try
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_started).count();
            Logger::Instance().Write(Level::Info, m_channel,
                L"OperationEnd " + m_name + L" duration=" + std::to_wstring(elapsed) + L"ms", m_location, m_id);
        }
        catch (...) {}
    }
}
