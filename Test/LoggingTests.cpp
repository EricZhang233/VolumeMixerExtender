#include "Logger.h"
#include <windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void Save(const std::filesystem::path& file, std::size_t size, std::chrono::hours age)
    {
        { std::ofstream output(file, std::ios::binary); output << std::string(size, 'x'); }
        std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() - age);
    }
}

int main()
{
    auto root = std::filesystem::temp_directory_path() / (L"vmex-logging-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    try
    {
        using namespace vmex::log;
        auto& logger = Logger::Instance();
        auto memory = CreateMemorySink(16);
        logger.AddSink(memory);
        logger.SetMinimumLevel(Level::Info);
        logger.Write(Level::Debug, L"probe", L"filtered");
        logger.Write(Level::Info, L"probe", L"githubToken = 'plain_secret' ghp_abcdef github_pat_abcdef");
        { OperationTimer timer(L"probe", L"FixtureOperation", L"fixture-operation"); }
        const auto records = MemorySinkSnapshot(memory);
        Require(records.size() == 3, "Level filtering or operation lifecycle changed");
        Require(records[0].text.find(L"secret") == std::wstring::npos && records[0].text.find(L"abcdef") == std::wstring::npos,
            "Credential values must be redacted before reaching any sink");
        Require(records[0].processId == ::GetCurrentProcessId() && records[0].threadId != 0 &&
            records[0].sourceFile == L"LoggingTests.cpp" && records[0].sourceLine != 0 && !records[0].function.empty(),
            "Logs must retain the caller, process and thread context");
        Require(records[1].operationId == L"fixture-operation" && records[2].operationId == records[1].operationId &&
            records[2].text.find(L"duration=") != std::wstring::npos, "Operations must retain a shared ID and elapsed time");
        Require(FormatLine(records[0]).find(L"Z probe/INFO]") != std::wstring::npos, "Timestamps must explicitly use UTC");
        Require(Redact(L"GITHUBTOKEN=plain\n\"githubToken\":\"another\" xghp_keep ghp_hide") ==
            L"GITHUBTOKEN=[redacted]\n\"githubToken\":\"[redacted]\" xghp_keep [redacted]", "Redaction boundaries changed");
        try { throw std::runtime_error("github_pat_exception"); }
        catch (...) { logger.WriteException(L"probe", L"Fixture exception"); }
        const auto exception = MemorySinkSnapshot(memory).back();
        Require(exception.text.find(L"runtime_error") != std::wstring::npos && exception.text.find(L"CatchStack=0x") != std::wstring::npos &&
            exception.text.find(L"github_pat_exception") == std::wstring::npos, "Exceptions must retain redacted type, detail and catch stack");
        logger.ClearSinks();

        Save(root / L"app-recent.log", 16, std::chrono::hours(1));
        Save(root / L"app-over-budget.log", 2048, std::chrono::hours(2));
        Save(root / L"app-expired.log", 16, std::chrono::hours(24 * 31));
        Save(root / L"unrelated.log", 16, std::chrono::hours(24 * 31));
        FileOptions options;
        options.queueCapacity = 1;
        options.retainedBytes = 1024;
        const auto file = root / L"app-stress.log";
        auto sink = CreateFileSink(file, options);
        Record record;
        record.time = std::chrono::system_clock::now();
        record.level = Level::Warn;
        record.text = L"rotation-probe|" + std::wstring(6 * 1024 * 1024, L'r') + L"|end";
        sink->Write(record);
        sink->Flush();
        Require(std::filesystem::exists(root / L"app-stress-1.log"), "A large entry must cause a five-megabyte roll");
        Require(!std::filesystem::exists(root / L"app-expired.log") && !std::filesystem::exists(root / L"app-over-budget.log") &&
            std::filesystem::exists(root / L"app-recent.log") && std::filesystem::exists(root / L"unrelated.log"),
            "Retention must enforce both limits without deleting unrelated or recent files");
        constexpr int count = 12000;
        record.text.clear();
        std::atomic_int next = 0;
        std::vector<std::thread> producers;
        for (int worker = 0; worker < 8; ++worker)
            producers.emplace_back([&]
            {
                for (;;)
                {
                    const int number = next++;
                    if (number >= count) break;
                    auto entry = record;
                    entry.text = L"critical-" + std::to_wstring(number) + L"|" + std::wstring(1024, L'x') + L"|end";
                    sink->Write(entry);
                }
            });
        for (auto& producer : producers) producer.join();
        record.level = Level::Debug;
        record.text = L"low-priority";
        for (int i = 0; i < 20000; ++i) sink->Write(record);
        record.level = Level::Error;
        record.text = L"flush-tail-marker";
        sink->Write(record);
        sink.reset();
        std::set<int> observed;
        bool tail = false, dropped = false, critical = false;
        for (const auto& item : std::filesystem::directory_iterator(root))
        {
            if (!item.path().filename().wstring().starts_with(L"app-stress")) continue;
            critical |= item.path().filename().wstring().find(L"critical") != std::wstring::npos;
            std::ifstream input(item.path(), std::ios::binary);
            std::string line;
            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                Require(!line.starts_with("\xEF\xBB\xBF"), "Log output must use UTF-8 without a BOM");
                if (const auto position = line.find("critical-"); position != std::string::npos)
                {
                    observed.insert(std::stoi(line.substr(position + 9)));
                    Require(line.ends_with("|end"), "Concurrent fallback writes must preserve complete lines");
                }
                tail |= line.find("flush-tail-marker") != std::string::npos;
                dropped |= line.find("Dropped low-priority log entries count=") != std::string::npos;
            }
        }
        Require(observed.size() == count && critical, "Every warning must survive bounded-queue overflow");
        Require(tail && dropped, "Shutdown must drain the final error and report low-priority losses");
        std::filesystem::remove_all(root);
        std::cout << "Logging stress checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
}
