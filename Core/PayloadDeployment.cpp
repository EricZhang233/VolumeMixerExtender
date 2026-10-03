#include "PayloadDeployment.h"

#include "ConfigService.h"
#include "InjectionContract.h"
#include "Logger.h"
#include "PayloadResources.h"
#include "Platform.h"

#include <system_error>

namespace vmex::inject
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"payload";
    }

    Status DeployPayloads(const std::filesystem::path& root, unsigned long sessionId, Deployment& deployment)
    {
        std::error_code error;
        wchar_t digest[24] = {};
        ::swprintf_s(digest, L"%016llX", static_cast<unsigned long long>(payload::PayloadContentHash()));
        const std::wstring version = L"payload-" + std::wstring(digest) + L"-" +
                                     std::to_wstring(payload::Size(payload::Item::Launcher)) + L"-" +
                                     std::to_wstring(payload::Size(payload::Item::Tap));

        deployment.directory = root / version;
        deployment.launcher = deployment.directory / payload::DefaultFileName(payload::Item::Launcher);
        deployment.tap = deployment.directory / payload::DefaultFileName(payload::Item::Tap);
        deployment.config = deployment.directory / std::wstring(kTapConfigFileName);

        std::filesystem::create_directories(deployment.directory, error);
        if (error)
        {
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Error, kChannel, L"log.payload.directory_failed", { deployment.directory.wstring() });
            return Status::Failed(L"payload-directory");
        }

        for (const auto item : { payload::Item::Launcher, payload::Item::Tap })
        {
            const auto target = item == payload::Item::Launcher ? deployment.launcher : deployment.tap;
            const auto expected = static_cast<std::uint32_t>(std::filesystem::file_size(target, error));
            if (!error && expected == payload::Size(item))
            {
                continue;
            }
            error.clear();

            const auto status = payload::Extract(item, target);
            if (!status.IsOk())
            {
                return status;
            }
        }

        config::ConfigService config;
        const auto loaded = config.Load(deployment.config);
        if (!loaded.IsOk())
        {
            return loaded;
        }

        config.SetString(std::wstring(kTapConfigSection), std::wstring(kConfigKeyTap), std::wstring(kTapDllName));
        config.SetString(std::wstring(kTapConfigSection), std::wstring(kConfigKeySession), std::to_wstring(sessionId));
        config.SetInt(std::wstring(kTapConfigSection), std::wstring(kConfigKeyEndpoint), 1);

        const auto saved = config.SaveAs(deployment.config);
        if (!saved.IsOk())
        {
            return saved;
        }

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info, kChannel, L"log.payload.deployed", { deployment.directory.wstring() });
        return Status::Ok();
    }

    Options BuildInjectionOptions(const Deployment& deployment, std::uint32_t timeoutMs)
    {
        Options options;
        options.launcherModule = deployment.launcher;
        options.tapModule = deployment.tap;
        options.configModule = deployment.config;
        options.timeoutMs = timeoutMs;
        return options;
    }
}
