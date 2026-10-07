#include "PayloadResources.h"

#include "Logger.h"
#include "Platform.h"

#include <windows.h>

namespace vmex::payload
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"payload";

        HRSRC Locate(Item item)
        {
            return ::FindResourceW(nullptr, MAKEINTRESOURCEW(ResourceId(item)), RT_RCDATA);
        }
    }

    std::wstring_view ToString(Item item)
    {
        if (item == Item::Launcher) return L"launcher";
        if (item == Item::Tap) return L"tap";
        if (item == Item::Diagnostics) return L"diagnostics";
        if (item == Item::NotificationToolkit) return L"notification-toolkit";
        return L"core";
    }

    unsigned ResourceId(Item item)
    {
        if (item == Item::Launcher) return VMEX_PAYLOAD_ID_LAUNCHER;
        if (item == Item::Tap) return VMEX_PAYLOAD_ID_TAP;
        if (item == Item::Diagnostics) return VMEX_PAYLOAD_ID_DIAGNOSTICS;
        if (item == Item::NotificationToolkit) return VMEX_PAYLOAD_ID_NOTIFICATION_TOOLKIT;
        return VMEX_PAYLOAD_ID_CORE;
    }

    std::filesystem::path DefaultFileName(Item item)
    {
        if (item == Item::Launcher) return L"vmex_launcher.dll";
        if (item == Item::Tap) return L"vmex_tap.dll";
        if (item == Item::Diagnostics) return L"xamldiagnostics.dll";
        if (item == Item::NotificationToolkit) return L"EricNotificationToolkit.ps1";
        return L"vmex_core.dll";
    }

    bool Exists(Item item)
    {
        return Locate(item) != nullptr;
    }

    std::uint32_t Size(Item item)
    {
        const auto resource = Locate(item);
        if (resource == nullptr)
        {
            return 0;
        }
        return ::SizeofResource(nullptr, resource);
    }

    namespace
    {
        std::uint64_t HashLocked(std::uint64_t seed, Item item)
        {
            const auto resource = Locate(item);
            if (resource == nullptr)
            {
                return seed;
            }

            const auto handle = ::LoadResource(nullptr, resource);
            if (handle == nullptr)
            {
                return seed;
            }

            const auto size = static_cast<std::size_t>(::SizeofResource(nullptr, resource));
            const auto data = static_cast<const std::byte*>(::LockResource(handle));
            if (data == nullptr || size == 0)
            {
                return seed;
            }

            std::uint64_t value = seed;
            for (std::size_t index = 0; index < size; ++index)
            {
                value ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(data[index]));
                value *= 0x100000001B3ull;
            }
            return value;
        }
    }

    std::uint64_t PayloadContentHash()
    {
        auto seed = HashLocked(0xCBF29CE484222325ull, Item::Launcher);
        seed = HashLocked(seed, Item::Tap);
        seed = HashLocked(seed, Item::Diagnostics);
        seed = HashLocked(seed, Item::NotificationToolkit);
        return HashLocked(seed, Item::Core);
    }

    Status Extract(Item item, const std::filesystem::path& destination)
    {
        const auto resource = Locate(item);
        if (resource == nullptr)
        {
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Error, kChannel, L"log.payload.missing", { std::wstring(ToString(item)) });
            return Status::Failed(L"missing embedded payload");
        }

        const auto handle = ::LoadResource(nullptr, resource);
        if (handle == nullptr)
        {
            return Status::FromLastError(L"LoadResource");
        }

        const auto size = ::SizeofResource(nullptr, resource);
        const auto data = static_cast<const std::byte*>(::LockResource(handle));
        if (data == nullptr || size == 0)
        {
            return Status::Failed(L"LockResource");
        }

        const auto status = platform::WriteAllBytes(destination, std::span<const std::byte>(data, size));
        if (!status.IsOk())
        {
            return status;
        }

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info,
            kChannel,
            L"log.payload.extracted",
            { std::wstring(ToString(item)), destination.wstring(), std::to_wstring(size) });
        return Status::Ok();
    }

    Status ExtractPair(const std::filesystem::path& directory, ExtractedPair& pair)
    {
        pair.launcher = directory / DefaultFileName(Item::Launcher);
        pair.tap = directory / DefaultFileName(Item::Tap);
        pair.diagnostics = directory / DefaultFileName(Item::Diagnostics);

        const auto launcherStatus = Extract(Item::Launcher, pair.launcher);
        if (!launcherStatus.IsOk())
        {
            return launcherStatus;
        }

        const auto tapStatus = Extract(Item::Tap, pair.tap);
        if (!tapStatus.IsOk()) return tapStatus;
        return Extract(Item::Diagnostics, pair.diagnostics);
    }

    Status ExtractNotificationToolkit(const std::filesystem::path& destination)
    {
        return Extract(Item::NotificationToolkit, destination);
    }
}
