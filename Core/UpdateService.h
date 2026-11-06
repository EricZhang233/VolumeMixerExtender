#pragma once

#include "Foundation.h"
#include <array>
#include <condition_variable>
#include <stop_token>
#include <thread>

namespace vmex::update
{
    inline constexpr std::uint64_t kCheckIntervalSeconds = 12 * 60 * 60;

    struct Release final
    {
        std::wstring version;
        std::wstring tag;
        std::wstring url;
        std::wstring digest;
        std::uint64_t size = 0;
    };

    [[nodiscard]] std::optional<std::array<unsigned, 4>> ParseVersion(std::wstring_view value);
    [[nodiscard]] bool ValidDigest(std::wstring_view value);
    [[nodiscard]] bool CheckDue(std::uint64_t last, std::uint64_t now);
    [[nodiscard]] std::optional<Release> SelectRelease(std::wstring_view json, std::wstring_view current);
    Status Check(std::optional<Release>& release, std::stop_token stop = {});
    Status ExtractUpdater(const std::filesystem::path& executable);
    [[nodiscard]] std::wstring ReleasePage(const Release& release);
    Status Launch(const Release& release, std::uint32_t managerProcessId = 0, void** processHandle = nullptr);

    class InteractiveSession final
    {
    public:
        using Checker = std::function<Status(std::optional<Release>&)>;
        using Opener = std::function<Status(std::wstring_view)>;
        using Launcher = std::function<Status(const Release&)>;
        InteractiveSession(Checker checker = {}, Opener opener = {}, Launcher launcher = {});
        ~InteractiveSession();
        Status Check(bool& available);
        Status OpenRelease();
        Status Install();
        Status Poll(bool& running);

    private:
        std::mutex m_mutex;
        std::optional<Release> m_release;
        Checker m_checker;
        Opener m_opener;
        Launcher m_launcher;
        void* m_process = nullptr;
    };

    class Service final
    {
    public:
        ~Service();
        void Start();
        void Stop();

    private:
        std::jthread m_worker;
        std::mutex m_mutex;
        std::condition_variable_any m_wake;
    };
}
