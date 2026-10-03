#include "Launcher.h"

#include "InjectionContract.h"
#include "Logger.h"
#include "Platform.h"
#include "TextService.h"

#include <windows.h>
#include <objbase.h>

#include <array>
#include <mutex>
#include <string>
#include <string_view>

namespace vmex::launcher
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"launcher";
        constexpr std::wstring_view kXamlModuleName = L"Windows.UI.Xaml.dll";
        constexpr std::wstring_view kDiagModuleName = L"xamldiagnostics.dll";
        constexpr std::wstring_view kDiagSubDirectory = L"XamlDiagnostics";

        constexpr unsigned long kClaimGraceMs = 250;

        constexpr unsigned long kDefaultXamlWaitMs = 30000;
        constexpr unsigned long kEndpointProbeLimit = 10000;

#if defined(_M_ARM64)
        constexpr std::wstring_view kArchDirectory = L"arm64";
#elif defined(_M_IX86)
        constexpr std::wstring_view kArchDirectory = L"x86";
#else
        constexpr std::wstring_view kArchDirectory = L"x64";
#endif

        HMODULE g_self = nullptr;

        enum class Claim
        {
            None,
            SelfInjection,
            Explicit
        };

        std::mutex g_stateMutex;
        std::mutex g_runMutex;
        Claim g_claim = Claim::None;
        bool g_succeeded = false;
        HRESULT g_result = E_UNEXPECTED;
        std::once_flag g_logging;

        std::filesystem::path ModuleDirectory()
        {
            return ModulePath(g_self).parent_path();
        }

        std::wstring Hex(std::uint32_t value)
        {
            wchar_t buffer[16] = {};
            swprintf_s(buffer, L"0x%08X", value);
            return buffer;
        }

        void AttachLogging()
        {
            std::call_once(g_logging, []()
            {
                text::AttachEmbeddedToLogger();
                log::Logger::Instance().AddSink(
                    log::CreateFileSink(log::SessionLogFile(L"launcher")));
                log::Logger::Instance().SetMinimumLevel(log::Level::Trace);
            });
        }

        void Record(HRESULT result, std::wstring_view key, const std::vector<std::wstring>& arguments)
        {
            log::Logger::Instance().WriteKeyFormat(
                FAILED(result) ? log::Level::Error : log::Level::Info, kChannel, key, arguments);
        }

        std::wstring ReadConfigString(const std::filesystem::path& file, std::wstring_view key, std::wstring_view fallback)
        {
            std::wstring buffer(MAX_PATH, L'\0');
            for (;;)
            {
                const DWORD length = ::GetPrivateProfileStringW(
                    std::wstring(inject::kTapConfigSection).c_str(),
                    std::wstring(key).c_str(),
                    std::wstring(fallback).c_str(),
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    file.c_str());

                if (length + 1 < buffer.size())
                {
                    buffer.resize(length);
                    return buffer;
                }
                buffer.resize(buffer.size() * 2);
            }
        }

        std::filesystem::path ResolvePayloadPath(const std::filesystem::path& directory, const std::filesystem::path& value)
        {
            if (value.empty())
            {
                return {};
            }
            return value.is_absolute() ? value : directory / value;
        }

        std::filesystem::path LocateXamlDiagnostics()
        {
            const auto local = ModuleDirectory() / std::wstring(kDiagModuleName);
            if (platform::FileExists(local))
            {
                return local;
            }

            constexpr std::array<std::wstring_view, 2> kKitsRoots{
                L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
                L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows Kits\\Installed Roots",
            };

            for (const auto& key : kKitsRoots)
            {
                std::array<wchar_t, MAX_PATH> root{};
                DWORD bytes = static_cast<DWORD>(root.size() * sizeof(wchar_t));
                if (::RegGetValueW(HKEY_LOCAL_MACHINE, std::wstring(key).c_str(), L"KitsRoot10",
                                   RRF_RT_REG_SZ, nullptr, root.data(), &bytes) != ERROR_SUCCESS)
                {
                    continue;
                }

                const auto candidate = std::filesystem::path(root.data()) / L"bin" /
                                       std::wstring(kArchDirectory) / std::wstring(kDiagSubDirectory) /
                                       std::wstring(kDiagModuleName);
                if (platform::FileExists(candidate))
                {
                    return candidate;
                }
            }
            return {};
        }

        unsigned long ReadUnsigned(const std::filesystem::path& configModule, std::wstring_view key, unsigned long fallback)
        {
            const auto text = ReadConfigString(configModule, key, {});
            if (text.empty())
            {
                return fallback;
            }
            const unsigned long value = static_cast<unsigned long>(std::wcstoul(text.c_str(), nullptr, 10));
            return value == 0 ? fallback : value;
        }

        HMODULE WaitForXamlModule(unsigned long timeoutMs)
        {
            const ULONGLONG deadline = ::GetTickCount64() + timeoutMs;
            for (;;)
            {
                if (const HMODULE xaml = ::GetModuleHandleW(std::wstring(kXamlModuleName).c_str()))
                {
                    return xaml;
                }
                if (::GetTickCount64() >= deadline)
                {
                    return nullptr;
                }
                ::Sleep(100);
            }
        }

        void PushConfigToTap(const std::filesystem::path& tapModule, const std::filesystem::path& configModule)
        {
            const HMODULE tap = ::LoadLibraryW(tapModule.c_str());
            if (tap == nullptr)
            {
                const auto error = ::GetLastError();
                const HRESULT result = HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_MOD_NOT_FOUND : error);
                Record(result, L"log.launcher.tap_load_failed",
                       { tapModule.wstring(), std::to_wstring(error),
                         Hex(static_cast<std::uint32_t>(result)) });
                return;
            }

            const std::string exportName(inject::kTapInitDataExport);
            const auto push = reinterpret_cast<void(__stdcall*)(const wchar_t*)>(
                ::GetProcAddress(tap, exportName.c_str()));
            if (push == nullptr)
            {
                Record(E_FAIL, L"log.launcher.tap_export_missing",
                       { std::wstring(exportName.begin(), exportName.end()),
                         Hex(static_cast<std::uint32_t>(E_FAIL)) });
                return;
            }

            push(configModule.c_str());
        }

        HRESULT InjectCore(const std::filesystem::path& configOverride)
        {
            const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Info, kChannel, L"log.launcher.attached",
                { std::to_wstring(::GetCurrentProcessId()), Hex(static_cast<std::uint32_t>(com)) });

            const auto directory = ModuleDirectory();
            const auto configModule = configOverride.empty()
                ? directory / std::wstring(inject::kTapConfigFileName)
                : configOverride;

            const auto tapModule = ResolvePayloadPath(
                directory, ReadConfigString(configModule, inject::kConfigKeyTap, inject::kTapDllName));
            if (!platform::FileExists(tapModule))
            {
                const HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
                Record(result, L"log.launcher.tap_module_missing",
                       { tapModule.wstring(), Hex(static_cast<std::uint32_t>(result)) });
                return result;
            }

            const auto configuredDiag = ResolvePayloadPath(
                directory, ReadConfigString(configModule, inject::kConfigKeyDiag, {}));
            const auto diagnostics = configuredDiag.empty() ? LocateXamlDiagnostics() : configuredDiag;
            if (!platform::FileExists(diagnostics))
            {
                const HRESULT result = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
                Record(result, L"log.launcher.diag_missing",
                       { std::wstring(kDiagModuleName), configModule.wstring(),
                         Hex(static_cast<std::uint32_t>(result)) });
                return result;
            }

            const auto waitMs = ReadUnsigned(configModule, inject::kConfigKeyXamlWaitMs, kDefaultXamlWaitMs);
            const auto firstEndpoint = ReadUnsigned(configModule, inject::kConfigKeyEndpoint, 1);

            const HMODULE xaml = WaitForXamlModule(waitMs);
            if (xaml == nullptr)
            {
                const HRESULT result = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
                Record(result, L"log.launcher.xaml_not_loaded",
                       { std::wstring(kXamlModuleName), Hex(static_cast<std::uint32_t>(result)) });
                return result;
            }

            const std::string entryPoint(inject::kXamlEntryPointExport);
            const auto initialize = reinterpret_cast<InitializeXamlDiagnosticsExFn>(
                ::GetProcAddress(xaml, entryPoint.c_str()));
            if (initialize == nullptr)
            {
                const HRESULT result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
                Record(result, L"log.launcher.xaml_export_missing",
                       { std::wstring(kXamlModuleName), std::wstring(entryPoint.begin(), entryPoint.end()),
                         Hex(static_cast<std::uint32_t>(result)) });
                return result;
            }

            CLSID tapClsid{};
            const std::wstring classId(inject::kTapClassId);
            if (FAILED(::CLSIDFromString(classId.c_str(), &tapClsid)))
            {
                Record(E_INVALIDARG, L"log.launcher.class_id_invalid",
                       { classId, Hex(static_cast<std::uint32_t>(E_INVALIDARG)) });
                return E_INVALIDARG;
            }

            PushConfigToTap(tapModule, configModule);

            const std::wstring initData = L"cfg=" + configModule.wstring();

            log::Logger::Instance().WriteKeyFormat(
                log::Level::Info, kChannel, L"log.launcher.payload_paths",
                { tapModule.wstring(), diagnostics.wstring(), std::to_wstring(initData.size()) });

            for (unsigned long index = firstEndpoint; index < firstEndpoint + kEndpointProbeLimit; ++index)
            {
                const auto endpoint = inject::DiagEndPointName(index);
                const HRESULT hr = initialize(endpoint.c_str(), ::GetCurrentProcessId(),
                                              diagnostics.c_str(), tapModule.c_str(), tapClsid,
                                              initData.c_str());
                if (SUCCEEDED(hr))
                {
                    log::Logger::Instance().WriteKeyFormat(
                        log::Level::Info, kChannel, L"log.launcher.endpoint_success",
                        { endpoint, Hex(static_cast<std::uint32_t>(hr)) });
                    return S_OK;
                }

                if (hr == E_INVALIDARG)
                {
                    Record(hr, L"log.launcher.arguments_rejected",
                           { std::wstring(entryPoint.begin(), entryPoint.end()), endpoint,
                             Hex(static_cast<std::uint32_t>(hr)) });
                    return hr;
                }

                if (index < firstEndpoint + 3 || (index % 1000) == 0)
                {
                    log::Logger::Instance().WriteKeyFormat(
                        log::Level::Debug, kChannel, L"log.launcher.endpoint_probe",
                        { endpoint, Hex(static_cast<std::uint32_t>(hr)) });
                }
            }

            const HRESULT result = HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
            Record(result, L"log.launcher.endpoint_exhausted",
                   { Hex(static_cast<std::uint32_t>(result)) });
            return result;
        }

        HRESULT RunOnce(const std::filesystem::path& configOverride)
        {
            std::lock_guard<std::mutex> guard(g_runMutex);
            if (g_succeeded)
            {
                return S_OK;
            }

            AttachLogging();
            g_result = InjectCore(configOverride);
            if (SUCCEEDED(g_result))
            {
                g_succeeded = true;
            }
            return g_result;
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

    void SetSelfModule(HMODULE module)
    {
        g_self = module;
    }

    HRESULT Inject(const std::filesystem::path& configModule)
    {
        {
            std::lock_guard<std::mutex> guard(g_stateMutex);
            g_claim = Claim::Explicit;
        }
        return RunOnce(configModule);
    }

    void BeginSelfInjection()
    {
        const HANDLE thread = ::CreateThread(nullptr, 0, [](LPVOID) -> DWORD
        {
            ::Sleep(kClaimGraceMs);

            {
                std::lock_guard<std::mutex> guard(g_stateMutex);
                if (g_claim != Claim::None)
                {
                    return 0;
                }
                g_claim = Claim::SelfInjection;
            }

            RunOnce({});
            return 0;
        }, nullptr, 0, nullptr);

        if (thread != nullptr)
        {
            ::CloseHandle(thread);
        }
    }
}

extern "C" __declspec(dllexport) HRESULT __stdcall VmExtLauncherRun(const wchar_t* configModule)
{
    return vmex::launcher::Inject(configModule == nullptr
        ? std::filesystem::path{}
        : std::filesystem::path(configModule));
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        ::DisableThreadLibraryCalls(module);
        vmex::launcher::SetSelfModule(module);
        vmex::launcher::BeginSelfInjection();
    }
    return TRUE;
}
