#pragma once

#include "Log.h"
#include <exception>

namespace vmex::log
{
    class Logger final
    {
    public:
        [[nodiscard]] static Logger& Instance();
        void AddSink(const std::shared_ptr<ILogSink>& sink);
        void ClearSinks();
        [[nodiscard]] std::size_t SinkCount() const noexcept;
        void SetMinimumLevel(Level level);
        [[nodiscard]] Level MinimumLevel() const noexcept;
        void SetChannelPrefix(std::wstring_view prefix);
        void Write(Level level, std::wstring_view channel, std::wstring_view message,
            std::source_location location = std::source_location::current(), std::wstring_view operationId = {});
        void WriteFormat(Level level, std::wstring_view channel, std::wstring_view pattern,
            const std::vector<std::wstring>& arguments,
            std::source_location location = std::source_location::current(), std::wstring_view operationId = {});
        void WriteException(std::wstring_view channel, std::wstring_view message,
            std::exception_ptr error = std::current_exception(), std::source_location location = std::source_location::current());
        void Flush();

    private:
        Logger();
        ~Logger();
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    class OperationTimer final
    {
    public:
        OperationTimer(std::wstring_view channel, std::wstring_view name, std::wstring_view operationId = {},
            std::source_location location = std::source_location::current());
        ~OperationTimer();
        OperationTimer(const OperationTimer&) = delete;
        OperationTimer& operator=(const OperationTimer&) = delete;

    private:
        std::wstring m_channel, m_name, m_id;
        std::source_location m_location;
        std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
    };
}
