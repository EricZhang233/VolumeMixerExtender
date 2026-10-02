#include "Tap.h"

#include "Logger.h"
#include "Platform.h"

#include <windows.h>

#include <mutex>
#include <string>

namespace vmex::tap
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"tap";

        std::mutex g_mutex;
        std::once_flag g_logging;
        std::wstring g_initData;
        bool g_provided = false;

        void AttachLogging()
        {
            std::call_once(g_logging, []() {
                log::Logger::Instance().AddSink(log::CreateFileSink(platform::GetExecutableDirectory() / L"vmex_tap.log"));
                log::Logger::Instance().SetMinimumLevel(log::Level::Trace);
            });
        }
    }

    std::filesystem::path ConfigModulePath()
    {
        return platform::GetExecutableDirectory() / L"vmex_tap.ini";
    }

    const wchar_t* ProvideInitData()
    {
        std::lock_guard<std::mutex> guard(g_mutex);

        if (!g_provided)
        {
            g_provided = true;
            g_initData = L"cfg=";
            g_initData.append(ConfigModulePath().wstring());
        }
        return g_initData.c_str();
    }
}

extern "C" __declspec(dllexport) const wchar_t* __stdcall VmExtTapProvideInitData()
{
    vmex::tap::AttachLogging();
    return vmex::tap::ProvideInitData();
}

extern "C" HRESULT __stdcall DllGetClassObject(const CLSID&, const IID&, void**)
{
    return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT __stdcall DllCanUnloadNow()
{
    return S_FALSE;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        ::DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
