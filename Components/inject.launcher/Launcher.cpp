#include "Launcher.h"

#include "Logger.h"
#include "Platform.h"
#include "Strings.h"

#include <windows.h>

namespace vmex::launcher
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"launcher";
        constexpr std::wstring_view kTapExport = L"VmExtTapProvideInitData";

        void AttachLogging()
        {
            static bool attached = false;
            if (attached)
            {
                return;
            }
            attached = true;

            const auto directory = platform::GetExecutableDirectory();
            log::Logger::Instance().AddSink(log::CreateFileSink(directory / L"vmex_launcher.log"));
            log::Logger::Instance().SetMinimumLevel(log::Level::Trace);
        }
    }

    std::filesystem::path ModulePath(HMODULE module)
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const auto length = ::GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < buffer.size())
            {
                buffer.resize(length);
                return std::filesystem::path(buffer);
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    Status Run(const std::filesystem::path& tapModule)
    {
        AttachLogging();
        log::Logger::Instance().Write(log::Level::Info, kChannel, std::wstring(L"launcher attach, tap=" + tapModule.wstring()));

        const auto tap = ::LoadLibraryW(tapModule.c_str());
        if (tap == nullptr)
        {
            return Status::FromLastError(L"LoadLibraryW(tap)");
        }

        const auto provider = reinterpret_cast<ProvideInitDataFn>(::GetProcAddress(tap, "VmExtTapProvideInitData"));
        if (provider == nullptr)
        {
            return Status::Failed(std::wstring(L"missing export: ") + std::wstring(kTapExport));
        }

        const auto initData = provider();
        if (initData == nullptr)
        {
            return Status::Failed(L"init data provider returned null");
        }

        const auto length = std::wcslen(initData);
        log::Logger::Instance().Write(log::Level::Info, kChannel, std::wstring(L"init data length=" + std::to_wstring(length)));

        const auto xaml = ::GetModuleHandleW(L"Windows.UI.Xaml.dll");
        if (xaml == nullptr)
        {
            return Status::Failed(L"Windows.UI.Xaml.dll is not loaded in the target process");
        }

        const auto initialize = reinterpret_cast<InitializeXamlDiagnosticsExFn>(::GetProcAddress(xaml, "InitializeXamlDiagnosticsEx"));
        if (initialize == nullptr)
        {
            return Status::Failed(L"InitializeXamlDiagnosticsEx is not exported by Windows.UI.Xaml.dll");
        }

        return Status::NotSupported(L"InitializeXamlDiagnosticsEx invocation is not wired yet");
    }
}

extern "C" __declspec(dllexport) int __stdcall VmExtLauncherRun(const wchar_t* tapModule)
{
    if (tapModule == nullptr)
    {
        return vmex::kCodeInvalidArguments;
    }

    const auto status = vmex::launcher::Run(std::filesystem::path(tapModule));
    return status.code;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        ::DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
