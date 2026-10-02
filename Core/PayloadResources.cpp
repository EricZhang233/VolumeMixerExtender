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
        return item == Item::Launcher ? L"launcher" : L"tap";
    }

    unsigned ResourceId(Item item)
    {
        return item == Item::Launcher ? VMEX_PAYLOAD_ID_LAUNCHER : VMEX_PAYLOAD_ID_TAP;
    }

    std::filesystem::path DefaultFileName(Item item)
    {
        return item == Item::Launcher ? std::filesystem::path(L"vmex_launcher.dll") : std::filesystem::path(L"vmex_tap.dll");
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

    Status Extract(Item item, const std::filesystem::path& destination)
    {
        const auto resource = Locate(item);
        if (resource == nullptr)
        {
            log::Logger::Instance().Write(log::Level::Error, kChannel, std::wstring(L"missing embedded payload: ") + std::wstring(ToString(item)));
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

        log::Logger::Instance().Write(
            log::Level::Info,
            kChannel,
            std::wstring(L"extracted ") + std::wstring(ToString(item)) + L" -> " + destination.wstring() + L" (" + std::to_wstring(size) + L" bytes)");
        return Status::Ok();
    }

    Status ExtractPair(const std::filesystem::path& directory, ExtractedPair& pair)
    {
        pair.launcher = directory / DefaultFileName(Item::Launcher);
        pair.tap = directory / DefaultFileName(Item::Tap);

        const auto launcherStatus = Extract(Item::Launcher, pair.launcher);
        if (!launcherStatus.IsOk())
        {
            return launcherStatus;
        }

        return Extract(Item::Tap, pair.tap);
    }
}
