#pragma once

#include "Foundation.h"

#include <chrono>
#include <source_location>

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
        std::uint32_t processId = 0;
        std::wstring sourceFile;
        std::wstring function;
        std::uint32_t sourceLine = 0;
        std::wstring operationId;
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

    struct FileOptions final
    {
        std::size_t queueCapacity = 4096;
        std::size_t batchSize = 256;
        std::uint64_t partBytes = 5 * 1024 * 1024;
        std::uint64_t retainedBytes = 100 * 1024 * 1024;
        std::chrono::hours retention = std::chrono::hours(24 * 30);
    };

    [[nodiscard]] std::wstring Redact(std::wstring_view value);
    [[nodiscard]] std::wstring FormatLine(const Record& record);
    [[nodiscard]] std::shared_ptr<ILogSink> CreateFileSink(const std::filesystem::path& file, FileOptions options = {});

    [[nodiscard]] std::filesystem::path SessionLogFile(std::wstring_view role);
    [[nodiscard]] std::shared_ptr<ILogSink> CreateDebugSink();
    [[nodiscard]] std::shared_ptr<ILogSink> CreateMemorySink(std::size_t capacity);
    [[nodiscard]] std::vector<Record> MemorySinkSnapshot(const std::shared_ptr<ILogSink>& sink);
}
