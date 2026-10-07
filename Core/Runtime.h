#pragma once

#include "App.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace vmex::runtime
{
    [[nodiscard]] std::vector<std::wstring> CollectArguments(int argc, wchar_t** argv);
    [[nodiscard]] AppOptions BuildOptions(const std::vector<std::wstring>& arguments,
                                          bool autorun = false);
    [[nodiscard]] std::vector<std::wstring> RemoveGlobalOptions(
        const std::vector<std::wstring>& arguments);
    [[nodiscard]] bool HasAutorunArgument();

    class HostNotifications final
    {
    public:
        HostNotifications() = default;
        ~HostNotifications();

        HostNotifications(const HostNotifications&) = delete;
        HostNotifications& operator=(const HostNotifications&) = delete;

        void Start(App& app, bool autorun);
        void Stop();

    private:
        std::atomic<bool> m_stop{ false };
        std::thread m_thread;
    };
}
