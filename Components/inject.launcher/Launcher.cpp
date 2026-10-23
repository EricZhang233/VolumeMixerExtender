#include "Launcher.h"

#include "InjectionContract.h"
#include "Logger.h"
#include "Platform.h"
#include "TextService.h"

#include <windows.h>
#include <objbase.h>

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

        constexpr unsigned long kClaimGraceMs = 250;

        constexpr unsigned long kDefaultXamlWaitMs = 30000;
        constexpr unsigned long kEndpointProbeLimit = 10000;

        HMODULE g_self = nullptr;

        enum class Claim
        {
            None,
            SelfInjection
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

        std::filesystem::path LocateXamlDiagnostics()
        {
            return ModuleDirectory() / std::wstring(kDiagModuleName);
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

        void LoadTap(const std::filesystem::path& tapModule)
        {
            const HMODULE tap = ::LoadLibraryW(tapModule.c_str());
            if (tap == nullptr)
            {
                const auto error = ::GetLastError();
                const HRESULT result = HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_MOD_NOT_FOUND : error);
                Record(result, L"log.launcher.tap_load_failed",
                       { tapModule.wstring(), std::to_wstring(error),
                         Hex(static_cast<std::uint32_t>(result)) });
            }
        }

        HRESULT InjectCore()
        {
            const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Info, kChannel, L"log.launcher.attached",
                { std::to_wstring(::GetCurrentProcessId()), Hex(static_cast<std::uint32_t>(com)) });

            const auto directory = ModuleDirectory();
            const auto tapModule = directory / std::wstring(inject::kTapDllName);
            if (!platform::FileExists(tapModule))
            {
                const HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
                Record(result, L"log.launcher.tap_module_missing",
                       { tapModule.wstring(), Hex(static_cast<std::uint32_t>(result)) });
                return result;
            }

            const auto diagnostics = LocateXamlDiagnostics();
            if (!platform::FileExists(diagnostics))
            {
                const HRESULT result = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
                Record(result, L"log.launcher.diag_missing",
                       { std::wstring(kDiagModuleName), diagnostics.wstring(),
                         Hex(static_cast<std::uint32_t>(result)) });
                return result;
            }

            const HMODULE xaml = WaitForXamlModule(kDefaultXamlWaitMs);
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

            LoadTap(tapModule);

            log::Logger::Instance().WriteKeyFormat(
                log::Level::Info, kChannel, L"log.launcher.payload_paths",
                { tapModule.wstring(), diagnostics.wstring() });

            for (unsigned long index = 1; index < 1 + kEndpointProbeLimit; ++index)
            {
                const auto endpoint = inject::DiagEndPointName(index);
                const HRESULT hr = initialize(endpoint.c_str(), ::GetCurrentProcessId(),
                                              diagnostics.c_str(), tapModule.c_str(), tapClsid,
                                              L"");
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

                if (index < 4 || (index % 1000) == 0)
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

        HRESULT RunOnce()
        {
            std::lock_guard<std::mutex> guard(g_runMutex);
            if (g_succeeded)
            {
                return S_OK;
            }

            AttachLogging();
            g_result = InjectCore();
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

            RunOnce();
            return 0;
        }, nullptr, 0, nullptr);

        if (thread != nullptr)
        {
            ::CloseHandle(thread);
        }
    }
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
