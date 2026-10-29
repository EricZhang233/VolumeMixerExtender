#include "RecordingGuard.h"

#include "AudioInterop.h"
#include "AudioRoutingCleanup.h"
#include "Platform.h"
#include "Strings.h"
#include "RecordingDriverService.h"

namespace vmex::recording
{
    namespace
    {
        struct RecoveryTargets final
        {
            volatile LONG active;
            wchar_t defaults[2][2][4096];
        };
    }

    Status Guard::UpdateDefaults(const std::array<std::wstring, 2>& defaults)
    {
        if (!targets) return Status::Failed(L"recording-guard-targets");
        for (const auto& value : defaults)
            if (value.empty() || value.size() >= 4096) return Status::InvalidArguments(L"recording-guard-target");
        auto* view = static_cast<RecoveryTargets*>(::MapViewOfFile(targets, FILE_MAP_WRITE, 0, 0, sizeof(RecoveryTargets)));
        if (!view) return Status::FromLastError(L"recording-guard-target-map");
        const LONG next = 1 - view->active;
        for (size_t role = 0; role < defaults.size(); ++role)
            ::wcscpy_s(view->defaults[next][role], defaults[role].c_str());
        ::InterlockedExchange(&view->active, next);
        ::UnmapViewOfFile(view);
        return Status::Ok();
    }

    Status RecoverAfterHostExit(IRecoveryActions& actions)
    {
        Status result;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            try
            {
                result = actions.RestoreDefaults();
                if (result) result = actions.ClearRedirects();
                if (result) result = actions.DisableEndpoint();
            }
            catch (...) { result = Status::Failed(L"recording-recovery-exception"); }
            if (result) break;
            if (attempt < 2) ::Sleep(250);
        }
        return result;
    }

    Guard::~Guard() { Stop(); }

    Status Guard::Start(const std::array<std::wstring, 2>& defaults, std::wstring_view endpoint)
    {
        Stop();
        SECURITY_ATTRIBUTES attributes{ sizeof(attributes), nullptr, TRUE };
        stop = ::CreateEventW(&attributes, TRUE, FALSE, nullptr);
        targets = ::CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, sizeof(RecoveryTargets), nullptr);
        HANDLE ready = ::CreateEventW(&attributes, TRUE, FALSE, nullptr);
        HANDLE parent = nullptr;
        const bool duplicated = ::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentProcess(), ::GetCurrentProcess(),
            &parent, SYNCHRONIZE, TRUE, 0) != FALSE;
        if (!stop || !targets || !ready || !duplicated)
        {
            if (ready) ::CloseHandle(ready);
            if (parent) ::CloseHandle(parent);
            Stop();
            return Status::Failed(L"recording-guard-handles");
        }
        const auto saved = UpdateDefaults(defaults);
        if (!saved) { ::CloseHandle(ready); ::CloseHandle(parent); Stop(); return saved; }
        const auto executable = platform::GetExecutableDirectory() / L"vmex_cli.exe";
        std::wstring command = L"\"" + executable.wstring() + L"\" --recording-guard " +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(parent)) + L" " +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(stop)) + L" " +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(ready));
        for (const auto& value : { defaults[0], defaults[1], std::wstring(endpoint) })
        {
            if (value.empty() || value.find_first_of(L"\"\r\n") != std::wstring::npos)
            {
                ::CloseHandle(parent); ::CloseHandle(ready); Stop();
                return Status::InvalidArguments(L"recording-guard-endpoint");
            }
            command += L" \"" + value + L"\"";
        }
        command += L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(targets));
        SIZE_T bytes = 0;
        ::InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<std::byte> storage(bytes);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        HANDLE handles[]{ parent, stop, ready, targets };
        bool initialized = ::InitializeProcThreadAttributeList(list, 1, 0, &bytes) != FALSE;
        bool valid = initialized && ::UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            handles, sizeof(handles), nullptr, nullptr);
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.lpAttributeList = list;
        PROCESS_INFORMATION child{};
        if (valid) valid = ::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &child) != FALSE;
        if (initialized) ::DeleteProcThreadAttributeList(list);
        ::CloseHandle(parent);
        if (valid)
        {
            process = child.hProcess;
            ::CloseHandle(child.hThread);
            HANDLE waits[]{ ready, process };
            valid = ::WaitForMultipleObjects(2, waits, FALSE, 5000) == WAIT_OBJECT_0 && Running();
        }
        ::CloseHandle(ready);
        if (!valid) { Stop(); return Status::Failed(L"recording-guard-start"); }
        return Status::Ok();
    }

    void Guard::Stop() noexcept
    {
        if (stop) ::SetEvent(stop);
        if (process) { ::WaitForSingleObject(process, 5000); ::CloseHandle(process); process = nullptr; }
        if (stop) { ::CloseHandle(stop); stop = nullptr; }
        if (targets) { ::CloseHandle(targets); targets = nullptr; }
    }

    bool Guard::Running() const { return process && ::WaitForSingleObject(process, 0) == WAIT_TIMEOUT; }

    void Guard::Abandon() noexcept
    {
        if (process) { ::CloseHandle(process); process = nullptr; }
        if (stop) { ::CloseHandle(stop); stop = nullptr; }
        if (targets) { ::CloseHandle(targets); targets = nullptr; }
    }

    int RunGuard(const std::vector<std::wstring>& arguments)
    {
        if (arguments.size() != 8) return kCodeInvalidArguments;
        std::array<HANDLE, 4> handles{};
        for (size_t i = 0; i < handles.size(); ++i)
        {
            std::int64_t value = 0;
            if (!strings::TryParseInt(arguments[i == 3 ? 7 : i + 1], value) || value <= 0) return kCodeInvalidArguments;
            handles[i] = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
            DWORD flags = 0;
            if (!::GetHandleInformation(handles[i], &flags)) return kCodeInvalidArguments;
        }
        const auto com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(com)) return kCodeFailed;
        text::AttachEmbeddedToLogger();
        log::Logger::Instance().AddSink(log::CreateFileSink(log::SessionLogFile(L"recording-guard")));
        ::SetEvent(handles[2]);
        const auto wait = ::WaitForMultipleObjects(2, handles.data(), FALSE, INFINITE);
        Status result;
        if (wait == WAIT_OBJECT_0)
        {
            auto recoveryArguments = arguments;
            const auto* view = static_cast<const RecoveryTargets*>(::MapViewOfFile(handles[3], FILE_MAP_READ, 0, 0, sizeof(RecoveryTargets)));
            if (view)
            {
                const LONG active = view->active;
                if (active == 0 || active == 1)
                    for (size_t role = 0; role < 2; ++role)
                        if (::wcsnlen_s(view->defaults[active][role], 4096) < 4096 && view->defaults[active][role][0])
                            recoveryArguments[role + 4] = view->defaults[active][role];
                ::UnmapViewOfFile(view);
            }
            struct Recovery final : IRecoveryActions
            {
                const std::vector<std::wstring>& arguments;
                explicit Recovery(const std::vector<std::wstring>& values) : arguments(values) {}
                Status RestoreDefaults() override
                {
                    Status restored;
                    for (int i = 0; i < 2; ++i)
                    {
                        auto enumerator = audio::detail::MakeEnumerator();
                        audio::detail::Ptr<IMMDevice> current;
                        if (!enumerator || FAILED(enumerator->GetDefaultAudioEndpoint(eRender, static_cast<ERole>(i), current.put())))
                        {
                            restored = Status::Failed(L"recording-guard-default-query");
                            continue;
                        }
                        LPWSTR id = nullptr;
                        if (FAILED(current->GetId(&id))) { restored = Status::Failed(L"recording-guard-default-id"); continue; }
                        if (audio::detail::TakeDeviceId(id) != arguments[6]) continue;
                        const auto result = audio::detail::SetSystemDefaultEndpointForRoleResult(arguments[i + 4], static_cast<ERole>(i));
                        if (FAILED(result)) restored = Status::FromHResult(L"recording-guard-restore", result);
                    }
                    return restored;
                }
                Status ClearRedirects() override { return ClearVirtualRedirects(arguments[6]); }
                Status DisableEndpoint() override { return SetRecordingEndpointEnabled(arguments[6], false); }
            } recovery(recoveryArguments);
            result = RecoverAfterHostExit(recovery);
            log::Logger::Instance().WriteKeyFormat(result ? log::Level::Info : log::Level::Error,
                L"recording", L"log.recording.result", { L"guard-recover", std::to_wstring(result.code), result.detail });
        }
        else if (wait != WAIT_OBJECT_0 + 1) result = Status::Failed(L"recording-guard-wait");
        for (const auto handle : handles) ::CloseHandle(handle);
        log::Logger::Instance().Flush();
        ::CoUninitialize();
        return result.code;
    }
}
