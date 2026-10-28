#include "App.h"
#include "ManagementService.h"
#include "TextService.h"
#include "RecordingDriverService.h"
#include "AboutService.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>

namespace
{
    void Copy(std::wstring_view value, wchar_t* buffer, int capacity)
    {
        if (!buffer || capacity < 1) return;
        const auto length = std::min(value.size(), static_cast<std::size_t>(capacity - 1));
        std::copy_n(value.data(), length, buffer);
        buffer[length] = 0;
    }
}

extern "C" __declspec(dllexport) int VmexManagerInitialize(wchar_t* error, int capacity) noexcept
{
    try
    {
        vmex::AppOptions options;
        options.saveConfig = false;
        options.logFile = vmex::log::SessionLogFile(L"manager");
        const auto result = vmex::App::Instance().Initialize(options);
        Copy(result.detail, error, capacity);
        return result.code;
    }
    catch (...) { Copy(L"management-initialize", error, capacity); return vmex::kCodeFailed; }
}

extern "C" __declspec(dllexport) void VmexManagerShutdown() noexcept
{
    try { vmex::App::Instance().Shutdown(); } catch (...) {}
}

extern "C" __declspec(dllexport) void VmexManagerText(const wchar_t* key, wchar_t* buffer, int capacity) noexcept
{
    try { Copy(vmex::text::Embedded().Resolve(key ? key : L""), buffer, capacity); }
    catch (...) { Copy(L"", buffer, capacity); }
}

extern "C" __declspec(dllexport) void VmexManagerAbout(wchar_t* buffer, int capacity) noexcept
{
    try { Copy(vmex::about::Information(), buffer, capacity); }
    catch (...) { Copy(L"", buffer, capacity); }
}

extern "C" __declspec(dllexport) int VmexManagerQuery(int* values, int count) noexcept
{
    if (!values || count != 6) return vmex::kCodeInvalidArguments;
    struct Com final
    {
        HRESULT result = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~Com() { if (SUCCEEDED(result)) ::CoUninitialize(); }
    } com;
    if (FAILED(com.result)) return vmex::kCodeFailed;
    try
    {
        const auto state = vmex::management::Query();
        const int data[] = { state.running, state.responsive, state.injected, state.autostart,
                             state.showDriverName, static_cast<int>(state.processId) };
        std::copy_n(data, 6, values);
        return vmex::kCodeOk;
    }
    catch (...) { return vmex::kCodeFailed; }
}
extern "C" __declspec(dllexport) int VmexManagerExecute(int action, int value, wchar_t* error, int capacity) noexcept
{
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    vmex::Status result;
    try
    {
        if (FAILED(com)) result = vmex::Status::FromHResult(L"management-com", com);
        else switch (action)
        {
        case 1: result = vmex::management::StartHost(); break;
        case 2: result = vmex::management::StopHost(); break;
        case 3: result = vmex::management::SetAutostart(value != 0); break;
        case 4: result = vmex::management::SetShowDriverName(value != 0); break;
        case 5: result = vmex::management::OpenLogs(); break;
        case 6: result = vmex::management::OpenProject(); break;
        case 7: result = vmex::management::Uninstall(); break;
        case 8: result = vmex::management::OpenPanel(); break;
        case 9: result = vmex::management::RegisterStartMenu(); break;
        case 10: result = vmex::recording::InstallDriver(); break;
        case 11: result = vmex::recording::UninstallDriver(); break;
        default: result = vmex::Status::InvalidArguments(L"management-action"); break;
        }
    }
    catch (...) { result = vmex::Status::Failed(L"management-exception"); }
    if (SUCCEEDED(com)) ::CoUninitialize();
    Copy(result.detail, error, capacity);
    return result.code;
}

extern "C" __declspec(dllexport) int VmexRecordingDriverQuery(int* values, int count) noexcept
{
    if (!values || count != 4) return vmex::kCodeInvalidArguments;
    try
    {
        const auto state = vmex::recording::QueryDriver();
        const int data[] = { state.installed, state.started, state.packagePresent, static_cast<int>(state.problem) };
        std::copy_n(data, 4, values);
        return vmex::kCodeOk;
    }
    catch (...) { return vmex::kCodeFailed; }
}
