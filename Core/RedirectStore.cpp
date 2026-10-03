#include "RedirectStore.h"

#include "ConfigService.h"
#include "Logger.h"

namespace vmex::audio
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"redirect";
    }

    std::filesystem::path RedirectStorePath(const std::filesystem::path& cacheRoot)
    {
        return cacheRoot / std::wstring(kRedirectStoreFileName);
    }

    Status SetAppRedirect(const std::filesystem::path& cacheRoot, std::wstring_view appKey, std::wstring_view deviceId)
    {
        if (appKey.empty())
        {
            return Status::InvalidArguments(L"app-key");
        }

        const auto file = RedirectStorePath(cacheRoot);
        config::ConfigService config;
        const auto loaded = config.Load(file);
        if (!loaded.IsOk())
        {
            return loaded;
        }

        if (deviceId.empty())
        {
            config.Remove(std::wstring(kRedirectStoreSection), std::wstring(appKey));
        }
        else
        {
            config.SetString(std::wstring(kRedirectStoreSection), std::wstring(appKey), std::wstring(deviceId));
        }

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Debug,
            kChannel,
            L"log.redirect.stored",
            { std::wstring(appKey), std::wstring(deviceId) });
        return config.SaveAs(file);
    }

    std::wstring GetAppRedirect(const std::filesystem::path& cacheRoot, std::wstring_view appKey)
    {
        if (appKey.empty())
        {
            return {};
        }

        config::ConfigService config;
        if (!config.Load(RedirectStorePath(cacheRoot)).IsOk())
        {
            return {};
        }

        return config.GetString(std::wstring(kRedirectStoreSection), std::wstring(appKey), L"");
    }
}
