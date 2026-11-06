#include "Log.h"
#include "Platform.h"
#include "Strings.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <ctime>
#include <cwctype>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace vmex::log
{
    std::wstring_view ToString(Level level)
    {
        switch (level)
        {
        case Level::Trace: return L"TRACE";
        case Level::Debug: return L"DEBUG";
        case Level::Warn: return L"WARN";
        case Level::Error: return L"ERROR";
        case Level::Fatal: return L"FATAL";
        default: return L"INFO";
        }
    }

    bool TryParseLevel(std::wstring_view text, Level& level)
    {
        const auto value = strings::ToLower(strings::TrimView(text));
        for (auto candidate : { Level::Trace, Level::Debug, Level::Info, Level::Warn, Level::Error, Level::Fatal })
            if (value == strings::ToLower(ToString(candidate))) { level = candidate; return true; }
        if (value == L"warning") { level = Level::Warn; return true; }
        return false;
    }

    std::wstring Redact(std::wstring_view value)
    {
        auto word = [](wchar_t ch) { return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'_'; };
        std::wstring result(value);
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            if (result[i] != L'g' && result[i] != L'G') continue;
            std::wstring_view tail(result.data() + i, result.size() - i);
            if ((i == 0 || !word(result[i - 1])) && (tail.starts_with(L"ghp_") || tail.starts_with(L"github_pat_")))
            {
                const auto prefix = tail.starts_with(L"ghp_") ? 4 : 11;
                auto end = i + prefix;
                while (end < result.size() && word(result[end])) ++end;
                if (end > i + prefix) { result.replace(i, end - i, L"[redacted]"); i += 9; continue; }
            }
            constexpr std::wstring_view key = L"githubtoken";
            if (tail.size() < key.size() || !std::equal(key.begin(), key.end(), tail.begin(),
                [](wchar_t a, wchar_t b) { return a == ::towlower(b); })) continue;
            auto start = i + key.size();
            if (start < result.size() && (result[start] == L'\'' || result[start] == L'"')) ++start;
            while (start < result.size() && ::iswspace(result[start])) ++start;
            if (start == result.size() || (result[start] != L':' && result[start] != L'=')) continue;
            ++start;
            while (start < result.size() && ::iswspace(result[start])) ++start;
            if (start < result.size() && (result[start] == L'\'' || result[start] == L'"')) ++start;
            auto end = start;
            while (end < result.size() && !::iswspace(result[end]) && result[end] != L'\'' && result[end] != L'"') ++end;
            if (end > start) { result.replace(start, end - start, L"[redacted]"); i = start + 9; }
        }
        return result;
    }

    namespace
    {
        std::wstring Timestamp(std::chrono::system_clock::time_point time)
        {
            const auto seconds = std::chrono::system_clock::to_time_t(time);
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()) % 1000;
            std::tm utc{};
            gmtime_s(&utc, &seconds);
            wchar_t buffer[48]{};
            swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d:%02d.%03lldZ", utc.tm_year + 1900,
                utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
                static_cast<long long>(milliseconds.count()));
            return buffer;
        }

        struct Handle final
        {
            HANDLE value = INVALID_HANDLE_VALUE;
            ~Handle() { Reset(); }
            void Reset(HANDLE next = INVALID_HANDLE_VALUE)
            {
                if (value != INVALID_HANDLE_VALUE) ::CloseHandle(value);
                value = next;
            }
        };

        void Append(HANDLE file, const std::string& text)
        {
            std::size_t offset = 0;
            while (offset < text.size())
            {
                DWORD written = 0;
                const auto count = static_cast<DWORD>(std::min<std::size_t>(text.size() - offset, MAXDWORD));
                if (!::WriteFile(file, text.data() + offset, count, &written, nullptr) || !written)
                    throw std::runtime_error("log-write:" + std::to_string(::GetLastError()));
                offset += written;
            }
        }

        void Append(const std::filesystem::path& path, std::wstring_view line)
        {
            std::filesystem::create_directories(path.parent_path());
            Handle file{ ::CreateFileW(path.c_str(), FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
            if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("log-open");
            Append(file.value, strings::ToNarrow(Redact(line)) + "\r\n");
        }

        void Failure(std::wstring_view reason) noexcept
        {
            try
            {
                static std::mutex gate;
                std::lock_guard lock(gate);
                Append(std::filesystem::temp_directory_path() / L"volumemixerextender-log-error.txt",
                    Timestamp(std::chrono::system_clock::now()) + L" " + std::wstring(reason));
            }
            catch (...) {}
        }

        bool Managed(const std::filesystem::path& path)
        {
            if (path.extension() != L".log") return false;
            const auto name = path.filename().wstring();
            for (const auto prefix : { L"app-", L"manager-", L"tap-", L"launcher-", L"recording-guard-", L"updater-" })
                if (name.starts_with(prefix)) return true;
            return false;
        }

        void Retain(const std::filesystem::path& directory, const FileOptions& options)
        {
            struct Item { std::filesystem::path path; std::filesystem::file_time_type time; std::uint64_t size; };
            std::vector<Item> files;
            std::error_code error;
            for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
            {
                if (!it->is_regular_file(error) || !Managed(it->path())) continue;
                const auto time = it->last_write_time(error);
                const auto size = it->file_size(error);
                if (!error) files.push_back({ it->path(), time, size });
                error.clear();
            }
            std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.time > b.time; });
            std::uint64_t bytes = 0;
            for (const auto& file : files)
            {
                bytes += file.size;
                if (bytes > options.retainedBytes || std::filesystem::file_time_type::clock::now() - file.time > options.retention)
                    std::filesystem::remove(file.path, error);
            }
        }

        struct FileState final
        {
            struct Entry { Level level; std::wstring line; };
            std::filesystem::path file, critical;
            FileOptions options;
            std::mutex mutex, criticalGate;
            std::condition_variable wake, drained;
            std::deque<Entry> entries;
            std::uint64_t accepted = 0, completed = 0, dropped = 0;
            bool stopped = false, failed = false;

            void Critical(std::wstring_view line) noexcept
            {
                try { std::lock_guard lock(criticalGate); Append(critical, line); }
                catch (...) { Failure(L"Critical log write failed"); }
            }

            void Run()
            {
                Handle output;
                std::uint64_t bytes = 0;
                unsigned part = 0;
                auto open = [&]
                {
                    auto path = file;
                    if (part) path = file.parent_path() / (file.stem().wstring() + L"-" + std::to_wstring(part) + L".log");
                    output.Reset(::CreateFileW(path.c_str(), GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
                    if (output.value == INVALID_HANDLE_VALUE) throw std::runtime_error("log-part-open");
                    bytes = 0;
                };
                try
                {
                    std::filesystem::create_directories(file.parent_path());
                    Retain(file.parent_path(), options);
                    open();
                }
                catch (const std::exception& error)
                {
                    Failure(strings::ToWide(error.what()));
                    std::lock_guard lock(mutex); failed = true;
                }
                for (;;)
                {
                    std::vector<Entry> batch;
                    std::uint64_t lost = 0;
                    {
                        std::unique_lock lock(mutex);
                        wake.wait(lock, [&] { return stopped || !entries.empty() || dropped; });
                        if (stopped && entries.empty() && !dropped) break;
                        lost = std::exchange(dropped, 0);
                        while (batch.size() < options.batchSize && !entries.empty())
                        {
                            batch.push_back(std::move(entries.front())); entries.pop_front();
                        }
                    }
                    auto write = [&](const Entry& entry)
                    {
                        bool unavailable;
                        { std::lock_guard lock(mutex); unavailable = failed; }
                        if (!unavailable)
                        {
                            try
                            {
                                if (bytes >= options.partBytes) { ++part; open(); }
                                const auto encoded = strings::ToNarrow(entry.line) + "\r\n";
                                Append(output.value, encoded);
                                bytes += encoded.size();
                                return;
                            }
                            catch (const std::exception& error)
                            {
                                Failure(strings::ToWide(error.what()));
                                std::lock_guard lock(mutex); failed = true;
                            }
                        }
                        if (entry.level >= Level::Warn) Critical(entry.line);
                    };
                    for (const auto& entry : batch) write(entry);
                    if (lost) write({ Level::Warn, Timestamp(std::chrono::system_clock::now()) +
                        L" [Log/WARN] | Dropped low-priority log entries count=" + std::to_wstring(lost) });
                    if (output.value != INVALID_HANDLE_VALUE) ::FlushFileBuffers(output.value);
                    if (bytes >= options.partBytes)
                    {
                        try { ++part; open(); }
                        catch (const std::exception& error)
                        {
                            Failure(strings::ToWide(error.what()));
                            std::lock_guard lock(mutex); failed = true;
                        }
                    }
                    {
                        std::lock_guard lock(mutex); completed += batch.size();
                    }
                    drained.notify_all();
                }
            }
        };

        struct WorkerContext { std::shared_ptr<FileState> state; HMODULE module = nullptr; };

        DWORD WINAPI RunWriter(void* value)
        {
            auto context = std::unique_ptr<WorkerContext>(static_cast<WorkerContext*>(value));
            const auto module = context->module;
            try { context->state->Run(); }
            catch (...) { Failure(L"Log writer stopped unexpectedly"); }
            context.reset();
            ::FreeLibraryAndExitThread(module, 0);
        }

        class FileSink final : public ILogSink
        {
        public:
            FileSink(const std::filesystem::path& file, FileOptions options) : state(std::make_shared<FileState>())
            {
                state->file = std::filesystem::absolute(file);
                state->critical = state->file.parent_path() / (file.stem().wstring() + L"-critical.log");
                state->options = options;
                state->options.queueCapacity = std::max<std::size_t>(1, options.queueCapacity);
                state->options.batchSize = std::max<std::size_t>(1, options.batchSize);
                auto context = std::make_unique<WorkerContext>();
                context->state = state;
                if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    reinterpret_cast<LPCWSTR>(&RunWriter), &context->module)) throw std::runtime_error("log-worker-module");
                worker = ::CreateThread(nullptr, 0, RunWriter, context.get(), 0, nullptr);
                if (!worker)
                {
                    ::FreeLibrary(context->module);
                    throw std::runtime_error("log-worker-start");
                }
                context.release();
            }

            ~FileSink() override
            {
                using ShutdownProbe = BOOLEAN(WINAPI*)();
                const auto shutdown = reinterpret_cast<ShutdownProbe>(
                    ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "RtlDllShutdownInProgress"));
                if (shutdown && shutdown()) { ::CloseHandle(worker); return; }
                { std::lock_guard lock(state->mutex); state->stopped = true; }
                state->wake.notify_one();
                if (::WaitForSingleObject(worker, 5000) != WAIT_OBJECT_0) Failure(L"Log shutdown drain timed out");
                ::CloseHandle(worker);
            }

            std::wstring_view Name() const override { return L"file"; }

            void Write(const Record& record) override
            {
                const auto line = FormatLine(record);
                {
                    std::lock_guard lock(state->mutex);
                    if (state->stopped) return;
                    if (!state->failed && state->entries.size() < state->options.queueCapacity)
                    {
                        state->entries.push_back({ record.level, line }); ++state->accepted;
                        state->wake.notify_one();
                        return;
                    }
                    if (record.level < Level::Warn)
                    {
                        ++state->dropped; state->wake.notify_one(); return;
                    }
                }
                state->Critical(line);
            }

            void Flush() override
            {
                std::unique_lock lock(state->mutex);
                const auto target = state->accepted;
                state->wake.notify_one();
                if (!state->drained.wait_for(lock, std::chrono::seconds(5), [&] { return state->completed >= target && !state->dropped; }))
                    Failure(L"Log flush timed out");
            }

        private:
            std::shared_ptr<FileState> state;
            HANDLE worker = nullptr;
        };

        class DebugSink final : public ILogSink
        {
        public:
            std::wstring_view Name() const override { return L"debug"; }
            void Write(const Record& record) override { const auto line = FormatLine(record) + L"\n"; ::OutputDebugStringW(line.c_str()); }
        };

        class MemorySink final : public ILogSink
        {
        public:
            explicit MemorySink(std::size_t capacity) : capacity(std::max<std::size_t>(1, capacity)) {}
            std::wstring_view Name() const override { return L"memory"; }
            void Write(const Record& record) override
            {
                std::lock_guard lock(mutex);
                records.push_back(record);
                while (records.size() > capacity) records.pop_front();
            }
            std::vector<Record> Snapshot() const { std::lock_guard lock(mutex); return { records.begin(), records.end() }; }
        private:
            std::size_t capacity;
            std::deque<Record> records;
            mutable std::mutex mutex;
        };
    }

    std::wstring FormatLine(const Record& record)
    {
        auto line = L"[" + Timestamp(record.time) + L" " + record.channel + L"/" + std::wstring(ToString(record.level)) + L"] " +
            record.sourceFile + L":" + std::to_wstring(record.sourceLine) + L"." + record.function +
            L" pid=" + std::to_wstring(record.processId) + L" tid=" + std::to_wstring(record.threadId) +
            L" seq=" + std::to_wstring(record.sequence);
        if (!record.operationId.empty()) line += L" op=" + record.operationId;
        return Redact(line + L" | " + record.text);
    }

    std::shared_ptr<ILogSink> CreateFileSink(const std::filesystem::path& file, FileOptions options)
    {
        return std::make_shared<FileSink>(file, options);
    }

    std::filesystem::path SessionLogFile(std::wstring_view role)
    {
        static std::atomic_uint64_t serial = 0;
        static const auto session = []
        {
            SYSTEMTIME time{}; ::GetSystemTime(&time);
            wchar_t stamp[64]{};
            swprintf_s(stamp, L"%04u%02u%02u-%02u%02u%02u-%03u-%lu", time.wYear, time.wMonth, time.wDay,
                time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, ::GetCurrentProcessId());
            GUID id{}; ::CoCreateGuid(&id);
            wchar_t guid[40]{}; ::StringFromGUID2(id, guid, 40);
            return std::wstring(stamp) + L"-" + guid;
        }();
        return platform::GetCacheDirectory() / L"log" /
            (std::wstring(role) + L"-" + session + L"-" + std::to_wstring(++serial) + L".log");
    }

    std::shared_ptr<ILogSink> CreateDebugSink() { return std::make_shared<DebugSink>(); }
    std::shared_ptr<ILogSink> CreateMemorySink(std::size_t capacity) { return std::make_shared<MemorySink>(capacity); }
    std::vector<Record> MemorySinkSnapshot(const std::shared_ptr<ILogSink>& sink)
    {
        const auto memory = std::dynamic_pointer_cast<MemorySink>(sink);
        return memory ? memory->Snapshot() : std::vector<Record>{};
    }
}
