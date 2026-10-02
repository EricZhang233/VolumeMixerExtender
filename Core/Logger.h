#pragma once

#include "Log.h"

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

        void SetResolver(const ITextResolver* resolver);
        void SetChannelPrefix(std::wstring_view prefix);

        void Write(Level level, std::wstring_view channel, std::wstring_view text);
        void WriteKey(Level level, std::wstring_view channel, std::wstring_view key);
        void WriteKeyFormat(Level level, std::wstring_view channel, std::wstring_view key, const std::vector<std::wstring>& arguments);

        void Flush();

    private:
        Logger();
        ~Logger();
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
