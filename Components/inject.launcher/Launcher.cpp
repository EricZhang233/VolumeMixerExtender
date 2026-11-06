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
                log::Logger::Instance().AddSink(
                    log::CreateFileSink(log::SessionLogFile(L"launcher")));
                log::Logger::Instance().SetMinimumLevel(log::Level::Trace);
            });
        }

        void Record(HRESULT result, std::wstring_view message, const std::vector<std::wstring>& arguments,
            std::source_location location = std::source_location::current())
        {
            log::Logger::Instance().WriteFormat(
                FAILED(result) ? log::Level::Error : log::Level::Info, kChannel, message, arguments, location);
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
                Record(result, L"TAP module load failed path={0} error={1} detail={2}",
                       { tapModule.wstring(), std::to_wstring(error),
                         Hex(static_cast<std::uint32_t>(result)) });
            }
        }

        HRESULT InjectCore()
        {
            const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            log::Logger::Instance().WriteFormat(
                log::Level::Info, kChannel, L"Launcher attached pid={0} CoInitializeEx={1}",
                { std::to_wstring(::GetCurrentProcessId()), Hex(static_cast<std::uint32_t>(com)) });

            const auto directory = ModuleDirectory();
            const auto tapModule = directory / std::wstring(inject::kTapDllName);
            if (!platform::FileExists(tapModule))
            {
                const HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
                Record(result, L"TAP module missing path={0} detail={1}",
                       { tapModule.wstring(), Hex(static_cast<std::uint32_t>(result)) });
                return result;
            }

            const auto diagnostics = LocateXamlDiagnostics();
            if (!platform::FileExists(diagnostics))
            {
                const HRESULT result = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
                Record(result, L"Diagnostics module missing name={0} path={1} detail={2}",
                       { std::wstring(kDiagModuleName), diagnostics.wstring(),
                         Hex(static_cast<std::uint32_t>(result)) });
                return result;
            }

            const HMODULE xaml = WaitForXamlModule(kDefaultXamlWaitMs);
            if (xaml == nullptr)
            {
                const HRESULT result = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
                Record(result, L"XAML module wait timed out name={0} detail={1}",
                       { std::wstring(kXamlModuleName), Hex(static_cast<std::uint32_t>(result)) });
                return result;
            }

            const std::string entryPoint(inject::kXamlEntryPointExport);
            const auto initialize = reinterpret_cast<InitializeXamlDiagnosticsExFn>(
                ::GetProcAddress(xaml, entryPoint.c_str()));
            if (initialize == nullptr)
            {
                const HRESULT result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
                Record(result, L"XAML export missing module={0} export={1} detail={2}",
                       { std::wstring(kXamlModuleName), std::wstring(entryPoint.begin(), entryPoint.end()),
                         Hex(static_cast<std::uint32_t>(result)) });
                return result;
            }

            CLSID tapClsid{};
            const std::wstring classId(inject::kTapClassId);
            if (FAILED(::CLSIDFromString(classId.c_str(), &tapClsid)))
            {
                Record(E_INVALIDARG, L"Invalid TAP class ID value={0} detail={1}",
                       { classId, Hex(static_cast<std::uint32_t>(E_INVALIDARG)) });
                return E_INVALIDARG;
            }

            LoadTap(tapModule);

            log::Logger::Instance().WriteFormat(
                log::Level::Info, kChannel, L"Injection payload paths tap={0} diagnostics={1}",
                { tapModule.wstring(), diagnostics.wstring() });

            for (unsigned long index = 1; index < 1 + kEndpointProbeLimit; ++index)
            {
                const auto endpoint = inject::DiagEndPointName(index);
                const HRESULT hr = initialize(endpoint.c_str(), ::GetCurrentProcessId(),
                                              diagnostics.c_str(), tapModule.c_str(), tapClsid,
                                              L"");
                if (SUCCEEDED(hr))
                {
                    log::Logger::Instance().WriteFormat(
                        log::Level::Info, kChannel, L"XAML diagnostics attached endpoint={0} detail={1}",
                        { endpoint, Hex(static_cast<std::uint32_t>(hr)) });
                    return S_OK;
                }

                if (hr == E_INVALIDARG)
                {
                    Record(hr, L"Diagnostics arguments rejected interface={0} endpoint={1} detail={2}",
                           { std::wstring(entryPoint.begin(), entryPoint.end()), endpoint,
                             Hex(static_cast<std::uint32_t>(hr)) });
                    return hr;
                }

                if (index < 4 || (index % 1000) == 0)
                {
                    log::Logger::Instance().WriteFormat(
                        log::Level::Debug, kChannel, L"Diagnostics endpoint probe endpoint={0} detail={1}",
                        { endpoint, Hex(static_cast<std::uint32_t>(hr)) });
                }
            }

            const HRESULT result = HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
            Record(result, L"Diagnostics endpoint probes exhausted detail={0}",
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
