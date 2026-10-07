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
        constexpr std::wstring_view kPayloadPrefix = L"payload-";

        void CleanupStalePayloads(const std::filesystem::path& root, const std::filesystem::path& active)
        {
            std::error_code error;
            if (!std::filesystem::is_directory(root, error))
            {
                return;
            }

            for (const auto& entry : std::filesystem::directory_iterator(root, error))
            {
                if (error)
                {
                    return;
                }
                if (!entry.is_directory(error) || error || entry.path() == active ||
                    entry.path().filename().wstring().rfind(kPayloadPrefix, 0) != 0)
                {
                    error.clear();
                    continue;
                }

                std::error_code removeError;
                std::filesystem::remove_all(entry.path(), removeError);
                if (removeError)
                {
                    log::Logger::Instance().WriteKeyFormat(
                        log::Level::Debug, kChannel, L"log.payload.cleanup_skipped", { entry.path().wstring() });
                }
            }
        }
    }

    Status DeployPayloads(const std::filesystem::path& root, unsigned long sessionId, Deployment& deployment)
    {
        std::error_code error;
        wchar_t digest[24] = {};
        ::swprintf_s(digest, L"%016llX", static_cast<unsigned long long>(payload::PayloadContentHash()));
        const std::wstring version = L"payload-" + std::wstring(digest) + L"-" +
                                     std::to_wstring(payload::Size(payload::Item::Launcher)) + L"-" +
                                     std::to_wstring(payload::Size(payload::Item::Tap)) + L"-" +
                                     std::to_wstring(payload::Size(payload::Item::Diagnostics)) + L"-" +
                                     std::to_wstring(payload::Size(payload::Item::Core));

        deployment.directory = root / version;
        deployment.launcher = deployment.directory / payload::DefaultFileName(payload::Item::Launcher);
        deployment.tap = deployment.directory / payload::DefaultFileName(payload::Item::Tap);
        deployment.diagnostics = deployment.directory / payload::DefaultFileName(payload::Item::Diagnostics);
        deployment.core = deployment.directory / payload::DefaultFileName(payload::Item::Core);
        deployment.config = deployment.directory / std::wstring(kTapConfigFileName);

        std::filesystem::create_directories(deployment.directory, error);
        if (error)
        {
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Error, kChannel, L"log.payload.directory_failed", { deployment.directory.wstring() });
            return Status::Failed(L"payload-directory");
        }

        for (const auto item : { payload::Item::Launcher, payload::Item::Tap,
                                 payload::Item::Diagnostics, payload::Item::Core })
        {
            const auto target = item == payload::Item::Launcher
                ? deployment.launcher
                : item == payload::Item::Tap ? deployment.tap
                : item == payload::Item::Diagnostics ? deployment.diagnostics : deployment.core;
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
        config.SetString(std::wstring(kTapConfigSection), std::wstring(kConfigKeyDiag), deployment.diagnostics.wstring());
        config.SetString(std::wstring(kTapConfigSection), std::wstring(kConfigKeySession), std::to_wstring(sessionId));
        config.SetInt(std::wstring(kTapConfigSection), std::wstring(kConfigKeyEndpoint), 1);

        const auto saved = config.SaveAs(deployment.config);
        if (!saved.IsOk())
        {
            return saved;
        }

        CleanupStalePayloads(root, deployment.directory);
        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info, kChannel, L"log.payload.deployed", { deployment.directory.wstring() });
        return Status::Ok();
    }

    Options BuildInjectionOptions(const Deployment& deployment, std::uint32_t timeoutMs)
    {
        Options options;
        options.launcherModule = deployment.launcher;
        options.tapModule = deployment.tap;
        options.coreModule = deployment.core;
        options.configModule = deployment.config;
        options.timeoutMs = timeoutMs;
        return options;
    }
}
