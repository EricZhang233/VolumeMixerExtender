#include "PayloadDeployment.h"

#include "Logger.h"
#include "PayloadResources.h"
#include "Platform.h"

namespace vmex::inject
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"payload";
    }

    Status DeployPayloads(Deployment& deployment)
    {
        const auto exeDirectory = platform::GetExecutableDirectory();
        deployment.launcher = exeDirectory / payload::DefaultFileName(payload::Item::Launcher);
        deployment.tap = exeDirectory / payload::DefaultFileName(payload::Item::Tap);
        deployment.diagnostics = exeDirectory / payload::DefaultFileName(payload::Item::Diagnostics);
        deployment.core = exeDirectory / payload::DefaultFileName(payload::Item::Core);

        for (const auto& file : { deployment.launcher, deployment.tap, deployment.diagnostics, deployment.core })
        {
            if (!platform::FileExists(file))
            {
                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Error, kChannel, L"log.payload.missing", { file.wstring() });
                return Status::Failed(L"missing payload file");
            }
        }

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info, kChannel, L"log.payload.deployed", { exeDirectory.wstring() });
        return Status::Ok();
    }

    Options BuildInjectionOptions(const Deployment& deployment, std::uint32_t timeoutMs)
    {
        Options options;
        options.launcherModule = deployment.launcher;
        options.tapModule = deployment.tap;
        options.coreModule = deployment.core;
        options.timeoutMs = timeoutMs;
        return options;
    }
}
