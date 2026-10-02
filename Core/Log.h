#pragma once

#include "Foundation.h"

#include <chrono>

namespace vmex::log
{
    enum class Level
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5
    };

    [[nodiscard]] std::wstring_view ToString(Level level);
    [[nodiscard]] bool TryParseLevel(std::wstring_view text, Level& level);

    struct Record final
    {
        Level level = Level::Info;
        std::wstring channel;
        std::wstring text;
        std::uint64_t sequence = 0;
        std::uint32_t threadId = 0;
        std::chrono::system_clock::time_point time{};
    };

    class ILogSink
    {
    public:
        virtual ~ILogSink() = default;

        [[nodiscard]] virtual std::wstring_view Name() const = 0;
        virtual void Write(const Record& record) = 0;
        virtual void Flush() {}
    };

    [[nodiscard]] std::shared_ptr<ILogSink> CreateFileSink(const std::filesystem::path& file);
    [[nodiscard]] std::shared_ptr<ILogSink> CreateDebugSink();
    [[nodiscard]] std::shared_ptr<ILogSink> CreateMemorySink(std::size_t capacity);
    [[nodiscard]] std::vector<Record> MemorySinkSnapshot(const std::shared_ptr<ILogSink>& sink);
}
