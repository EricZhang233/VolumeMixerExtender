#include "PayloadResources.h"

#include "Logger.h"
#include "Platform.h"

#include <system_error>

namespace vmex::payload
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"payload";

        std::filesystem::path SourcePath(Item item)
        {
            return platform::GetExecutableDirectory() / DefaultFileName(item);
        }
    }

    std::wstring_view ToString(Item item)
    {
        if (item == Item::Launcher) return L"launcher";
        if (item == Item::Tap) return L"tap";
        if (item == Item::Diagnostics) return L"diagnostics";
        return L"core";
    }

    std::filesystem::path DefaultFileName(Item item)
    {
        if (item == Item::Launcher) return L"vmex_launcher.dll";
        if (item == Item::Tap) return L"vmex_tap.dll";
        if (item == Item::Diagnostics) return L"xamldiagnostics.dll";
        return L"vmex_core.dll";
    }

    bool Exists(Item item)
    {
        return platform::FileExists(SourcePath(item));
    }

    std::uint32_t Size(Item item)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(SourcePath(item), error);
        return error ? 0 : static_cast<std::uint32_t>(size);
    }

    Status Extract(Item item, const std::filesystem::path& destination)
    {
        const auto source = SourcePath(item);
        std::vector<std::byte> bytes;
        const auto read = platform::ReadAllBytes(source, bytes);
        if (!read.IsOk())
        {
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Error, kChannel, L"log.payload.missing", { std::wstring(ToString(item)) });
            return Status::Failed(L"missing payload file");
        }

        const auto status = platform::WriteAllBytes(destination, bytes);
        if (!status.IsOk())
        {
            return status;
        }

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info,
            kChannel,
            L"log.payload.extracted",
            { std::wstring(ToString(item)), destination.wstring(), std::to_wstring(bytes.size()) });
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
}
