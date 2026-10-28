#pragma once

#include "Foundation.h"

namespace vmex::management
{
    struct Snapshot final
    {
        bool running = false;
        bool responsive = false;
        bool injected = false;
        bool autostart = false;
        bool showDriverName = true;
        std::uint32_t processId = 0;
    };

    struct HostLink final
    {
        void* pipe = nullptr;
        std::uint32_t processId = 0;

        HostLink() = default;
        ~HostLink();
        HostLink(HostLink&& other) noexcept;
        HostLink& operator=(HostLink&& other) noexcept;
        HostLink(const HostLink&) = delete;
        HostLink& operator=(const HostLink&) = delete;

        [[nodiscard]] bool Valid() const noexcept { return pipe != nullptr; }
        void Reset() noexcept;
    };

    [[nodiscard]] HostLink ConnectHost();
    [[nodiscard]] bool IsHostImage(const std::filesystem::path& image, const std::filesystem::path& directory);
    [[nodiscard]] Snapshot Query();
    Status StartHost();
    Status StopHost();
    Status OpenManager();
    Status OpenPanel();
    Status OpenLogs();
    Status OpenProject();
    Status SetAutostart(bool enabled);
    Status SetShowDriverName(bool enabled);
    Status RegisterStartMenu();
    Status Uninstall();
    Status StopInjection();
}
