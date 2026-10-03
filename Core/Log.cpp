#include "Log.h"

#include "Platform.h"
#include "Strings.h"

#include <windows.h>

#include <ctime>
#include <deque>
#include <mutex>
#include <system_error>

namespace vmex::log
{
    std::wstring_view ToString(Level level)
    {
        switch (level)
        {
        case Level::Trace: return L"TRACE";
        case Level::Debug: return L"DEBUG";
        case Level::Info: return L"INFO";
        case Level::Warn: return L"WARN";
        case Level::Error: return L"ERROR";
        case Level::Fatal: return L"FATAL";
        default: return L"INFO";
        }
    }

    bool TryParseLevel(std::wstring_view text, Level& level)
    {
        const auto lowered = strings::ToLower(strings::TrimView(text));
        if (lowered == L"trace") { level = Level::Trace; return true; }
        if (lowered == L"debug") { level = Level::Debug; return true; }
        if (lowered == L"info") { level = Level::Info; return true; }
        if (lowered == L"warn" || lowered == L"warning") { level = Level::Warn; return true; }
        if (lowered == L"error") { level = Level::Error; return true; }
        if (lowered == L"fatal") { level = Level::Fatal; return true; }
        return false;
    }

    namespace
    {
        std::wstring FormatTimestamp(std::chrono::system_clock::time_point time)
        {
            const auto seconds = std::chrono::system_clock::to_time_t(time);
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()) % 1000;

            std::tm local{};
            localtime_s(&local, &seconds);

            wchar_t buffer[64] = {};
            swprintf_s(
                buffer,
                L"%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                local.tm_year + 1900,
                local.tm_mon + 1,
                local.tm_mday,
                local.tm_hour,
                local.tm_min,
                local.tm_sec,
                static_cast<long long>(milliseconds.count()));
            return std::wstring(buffer);
        }

        std::wstring FormatLine(const Record& record)
        {
            std::wstring line = FormatTimestamp(record.time);
            line.append(L" [");
            line.append(ToString(record.level));
            line.append(L"] [");
            line.append(record.channel);
            line.append(L"] [tid=");
            line.append(std::to_wstring(record.threadId));
            line.append(L" seq=");
            line.append(std::to_wstring(record.sequence));
            line.append(L"] ");
            line.append(record.text);
            return line;
        }

        class FileSink final : public ILogSink
        {
        public:
            explicit FileSink(const std::filesystem::path& file)
                : m_file(file)
            {
                std::error_code error;
                const auto parent = m_file.parent_path();
                if (!parent.empty())
                {
                    std::filesystem::create_directories(parent, error);
                }
                Open();
            }

            ~FileSink() override
            {
                Flush();
                if (m_handle != INVALID_HANDLE_VALUE)
                {
                    ::CloseHandle(m_handle);
                }
            }

            [[nodiscard]] std::wstring_view Name() const override { return L"file"; }

            void Write(const Record& record) override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (m_handle == INVALID_HANDLE_VALUE)
                {
                    return;
                }

                auto line = FormatLine(record);
                line.append(L"\r\n");
                const auto utf8 = strings::ToNarrow(line);
                if (utf8.empty())
                {
                    return;
                }

                DWORD written = 0;
                ::WriteFile(m_handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            }

            void Flush() override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (m_handle != INVALID_HANDLE_VALUE)
                {
                    ::FlushFileBuffers(m_handle);
                }
            }

        private:
            void Open()
            {
                m_handle = ::CreateFileW(
                    m_file.c_str(),
                    FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr,
                    OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);

                if (m_handle == INVALID_HANDLE_VALUE)
                {
                    return;
                }

                LARGE_INTEGER size{};
                if (::GetFileSizeEx(m_handle, &size) == FALSE || size.QuadPart == 0)
                {
                    static constexpr unsigned char kBom[] = { 0xEF, 0xBB, 0xBF };
                    DWORD written = 0;
                    ::WriteFile(m_handle, kBom, sizeof(kBom), &written, nullptr);
                }
            }

            std::filesystem::path m_file;
            HANDLE m_handle = INVALID_HANDLE_VALUE;
            std::mutex m_mutex;
        };

        class DebugSink final : public ILogSink
        {
        public:
            [[nodiscard]] std::wstring_view Name() const override { return L"debug"; }

            void Write(const Record& record) override
            {
                auto line = FormatLine(record);
                line.append(L"\n");
                ::OutputDebugStringW(line.c_str());
            }
        };

        class MemorySink final : public ILogSink
        {
        public:
            explicit MemorySink(std::size_t capacity)
                : m_capacity(capacity == 0 ? 1 : capacity)
            {
            }

            [[nodiscard]] std::wstring_view Name() const override { return L"memory"; }

            void Write(const Record& record) override
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                m_records.push_back(record);
                while (m_records.size() > m_capacity)
                {
                    m_records.pop_front();
                }
            }

            [[nodiscard]] std::vector<Record> Snapshot() const
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                return std::vector<Record>(m_records.begin(), m_records.end());
            }

        private:
            std::size_t m_capacity = 0;
            std::deque<Record> m_records;
            mutable std::mutex m_mutex;
        };
    }

    std::shared_ptr<ILogSink> CreateFileSink(const std::filesystem::path& file)
    {
        return std::make_shared<FileSink>(file);
    }

    std::filesystem::path SessionLogFile(std::wstring_view role)
    {
        static const std::wstring stamp = []()
        {
            std::tm local{};
            const std::time_t now = std::time(nullptr);
            localtime_s(&local, &now);
            wchar_t buffer[32] = {};
            swprintf_s(
                buffer,
                L"%04d%02d%02d-%02d%02d%02d",
                local.tm_year + 1900,
                local.tm_mon + 1,
                local.tm_mday,
                local.tm_hour,
                local.tm_min,
                local.tm_sec);
            return std::wstring(buffer);
        }();

        const auto directory = platform::GetCacheDirectory() / L"log";
        std::error_code ignored;
        std::filesystem::create_directories(directory, ignored);

        const std::wstring stem = std::wstring(role) + L"-" + stamp;
        std::filesystem::path file = directory / (stem + L".log");
        for (int suffix = 2; std::filesystem::exists(file, ignored) && suffix < 100; ++suffix)
        {
            file = directory / (stem + L"-" + std::to_wstring(suffix) + L".log");
        }
        return file;
    }

    std::shared_ptr<ILogSink> CreateDebugSink()
    {
        return std::make_shared<DebugSink>();
    }

    std::shared_ptr<ILogSink> CreateMemorySink(std::size_t capacity)
    {
        return std::make_shared<MemorySink>(capacity);
    }

    std::vector<Record> MemorySinkSnapshot(const std::shared_ptr<ILogSink>& sink)
    {
        const auto memory = std::dynamic_pointer_cast<MemorySink>(sink);
        if (memory == nullptr)
        {
            return {};
        }
        return memory->Snapshot();
    }
}
