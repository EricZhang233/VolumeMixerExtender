#include "ManagementService.h"
#include "MaintenanceWorkflow.h"

#include "App.h"
#include "AutostartEntry.h"
#include "HostPresence.h"
#include "StartMenuEntry.h"
#include "InjectionContract.h"
#include "Logger.h"
#include "Platform.h"
#include "UninstallService.h"
#include "UserSettings.h"
#include "RecordingDriverService.h"

#include <windows.h>
#include <shellapi.h>
#include <array>

namespace vmex::management
{
    namespace
    {
        struct Handle final
        {
            HANDLE value = nullptr;
            ~Handle() { if (value && value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
        };

        class Operation final
        {
        public:
            Operation()
            {
                const auto name = L"Local\\VmExt.Management.S" + std::to_wstring(platform::GetCurrentSessionId());
                m_handle.value = ::CreateMutexW(nullptr, FALSE, name.c_str());
                if (m_handle.value)
                {
                    const DWORD wait = ::WaitForSingleObject(m_handle.value, 0);
                    m_locked = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
                }
            }
            ~Operation() { if (m_locked) ::ReleaseMutex(m_handle.value); }
            bool Locked() const { return m_locked; }
        private:
            Handle m_handle;
            bool m_locked = false;
        };

        Status Report(std::wstring_view operation, Status result)
        {
            log::Logger::Instance().WriteKeyFormat(result ? log::Level::Info : log::Level::Error,
                L"management", L"log.management.result", { std::wstring(operation), std::to_wstring(result.code), result.detail });
            return result;
        }

        HANDLE Connect(DWORD access = GENERIC_WRITE)
        {
            const auto name = inject::TapPipeName(platform::GetCurrentSessionId());
            return ::CreateFileW(name.c_str(), access, 0, nullptr, OPEN_EXISTING,
                                 SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        }

        bool Identify(HANDLE pipe, DWORD& pid)
        {
            if (!::GetNamedPipeServerProcessId(pipe, &pid)) return false;
            DWORD session = 0;
            if (!::ProcessIdToSessionId(pid, &session) || session != platform::GetCurrentSessionId()) return false;
            Handle process{ ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) };
            if (!process.value) return false;
            std::array<wchar_t, 32768> path{};
            DWORD size = static_cast<DWORD>(path.size());
            if (!::QueryFullProcessImageNameW(process.value, 0, path.data(), &size)) return false;
            const std::filesystem::path image(std::wstring(path.data(), size));
            return IsHostImage(image, platform::GetApplicationDirectory());
        }

        Status StopUnlocked()
        {
            if (host::HostPresence::IsRunning())
            {
                Handle pipe{ Connect() };
                if (pipe.value == INVALID_HANDLE_VALUE) return Status::FromLastError(L"host-connect");
                DWORD pid = 0;
                if (!Identify(pipe.value, pid)) return Status::Failed(L"host-identity");
                Handle process{ ::OpenProcess(SYNCHRONIZE, FALSE, pid) };
                if (!process.value) return Status::FromLastError(L"host-process");
                constexpr char command[] = "EXIT\n";
                DWORD written = 0;
                if (!::WriteFile(pipe.value, command, sizeof(command) - 1, &written, nullptr) || written != sizeof(command) - 1)
                    return Status::FromLastError(L"host-exit-write");
                ::CloseHandle(pipe.value);
                pipe.value = nullptr;
                if (::WaitForSingleObject(process.value, 10000) != WAIT_OBJECT_0)
                    return Status::Failed(L"host-exit-timeout");
            }
            return StopInjection();
        }

        Status Launch(std::wstring_view file)
        {
            const auto path = platform::GetApplicationDirectory() / file;
            if (!platform::FileExists(path)) return Status::Failed(L"executable-missing:" + path.wstring());
            std::wstring command = L"\"" + path.wstring() + L"\"";
            STARTUPINFOW startup{ sizeof(startup) };
            PROCESS_INFORMATION process{};
            if (!::CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                  path.parent_path().c_str(), &startup, &process))
                return Status::FromLastError(L"launch");
            ::CloseHandle(process.hThread);
            ::CloseHandle(process.hProcess);
            return Status::Ok();
        }

        Status Open(std::wstring_view target)
        {
            const auto result = reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", std::wstring(target).c_str(),
                                                                         nullptr, nullptr, SW_SHOWNORMAL));
            return result > 32 ? Status::Ok() : Status::Failed(L"shell-open:" + std::to_wstring(result));
        }

        Status RemovePath(const std::filesystem::path& path, bool directory)
        {
            std::error_code error;
            const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
            if (error || absolute.empty() || absolute == absolute.root_path()) return Status::Failed(L"cleanup-path");
            auto ancestor = absolute;
            while (ancestor != ancestor.root_path())
            {
                const DWORD attributes = ::GetFileAttributesW(ancestor.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    return Status::Failed(L"cleanup-reparse:" + ancestor.wstring());
                ancestor = ancestor.parent_path();
            }
            if (directory && std::filesystem::exists(absolute, error))
            {
                for (std::filesystem::recursive_directory_iterator it(absolute, error), end; !error && it != end; it.increment(error))
                {
                    const DWORD attributes = ::GetFileAttributesW(it->path().c_str());
                    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                        return Status::Failed(L"cleanup-reparse:" + it->path().wstring());
                }
                if (error) return Status::Failed(L"cleanup-enumerate:" + absolute.wstring());
            }
            if (directory) std::filesystem::remove_all(absolute, error);
            else std::filesystem::remove(absolute, error);
            return error ? Status::Failed(L"cleanup:" + absolute.wstring() + L":" + std::to_wstring(error.value())) : Status::Ok();
        }
    }

    bool IsHostImage(const std::filesystem::path& image, const std::filesystem::path& directory)
    {
        const auto normalized = image.lexically_normal();
        return _wcsicmp(normalized.parent_path().c_str(), directory.lexically_normal().c_str()) == 0 &&
            (_wcsicmp(normalized.filename().c_str(), L"vmex.exe") == 0 ||
             _wcsicmp(normalized.filename().c_str(), L"vmex_cli.exe") == 0);
    }

    Snapshot Query()
    {
        Snapshot result;
        result.running = host::HostPresence::IsRunning();
        Handle pipe{ Connect() };
        DWORD pid = 0;
        if (pipe.value != INVALID_HANDLE_VALUE && Identify(pipe.value, pid))
        {
            result.responsive = true;
            result.processId = pid;
        }
        if (App::Instance().IsReady())
        {
            inject::State state;
            if (App::Instance().Injection().QueryState(state)) result.injected = state.tapLoaded;
        }
        result.autostart = autostart::AutostartEntry::IsEnabled();
        result.showDriverName = settings::ReadBool(settings::kShowDriverName, true);
        return result;
    }

    HostLink::~HostLink() { Reset(); }

    void HostLink::Reset() noexcept
    {
        if (pipe)
        {
            ::CloseHandle(static_cast<HANDLE>(pipe));
            pipe = nullptr;
        }
        processId = 0;
    }

    HostLink::HostLink(HostLink&& other) noexcept
        : pipe(other.pipe)
        , processId(other.processId)
    {
        other.pipe = nullptr;
        other.processId = 0;
    }

    HostLink& HostLink::operator=(HostLink&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            pipe = other.pipe;
            processId = other.processId;
            other.pipe = nullptr;
            other.processId = 0;
        }
        return *this;
    }

    HostLink ConnectHost()
    {
        HostLink link;
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            const HANDLE pipe = Connect(GENERIC_READ | GENERIC_WRITE);
            DWORD pid = 0;
            if (pipe != INVALID_HANDLE_VALUE && Identify(pipe, pid))
            {
                link.pipe = pipe;
                link.processId = pid;
                return link;
            }
            if (pipe != INVALID_HANDLE_VALUE) ::CloseHandle(pipe);
            ::Sleep(10);
        }
        return link;
    }

    Status StopInjection()
    {
        auto& app = App::Instance();
        const auto recording = app.StopRecording();
        if (!recording) return recording;
        app.StopMonitoring();
        inject::State state;
        const auto queried = app.Injection().QueryState(state);
        if (!queried) return queried;
        if (!state.tapLoaded) return Status::Ok();
        const auto result = uninstall::TerminateShellHost(state.target.processId);
        return result.ok ? Status::Ok() : Status::Failed(result.detail);
    }
    Status StartHost()
    {
        Operation operation;
        if (!operation.Locked()) return Status::Failed(L"management-busy");
        if (host::HostPresence::IsRunning())
        {
            auto link = ConnectHost();
            return Report(L"start", link.Valid() ? Status::Ok() : Status::Failed(L"host-identity"));
        }
        const auto launched = Launch(L"vmex.exe");
        if (!launched) return Report(L"start", launched);
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            if (host::HostPresence::IsRunning()) return Report(L"start", Status::Ok());
            ::Sleep(100);
        }
        return Report(L"start", Status::Failed(L"host-start-timeout"));
    }

    Status StopHost()
    {
        Operation operation;
        if (!operation.Locked()) return Status::Failed(L"management-busy");
        return Report(L"stop", StopUnlocked());
    }

    Status OpenManager() { return Report(L"manager", Launch(L"vmex_manager.exe")); }
    Status OpenPanel()
    {
        const auto started = StartHost();
        if (!started) return started;
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            inject::State state;
            if (App::Instance().Injection().QueryState(state) && state.tapLoaded)
                return Report(L"panel", Open(L"ms-actioncenter:controlcenter/volume"));
            ::Sleep(100);
        }
        return Report(L"panel", Status::Failed(App::Instance().Text().Resolve(L"manager.panel.pending")));
    }
    Status OpenLogs() { return Report(L"logs", Open((platform::GetCacheDirectory() / L"log").wstring())); }
    Status OpenProject() { return Open(L"https://github.com/EricZhang233/VolumeMixerExtender"); }

    Status SetAutostart(bool enabled)
    {
        Operation operation;
        if (!operation.Locked()) return Status::Failed(L"management-busy");
        std::wstring error;
        const bool applied = enabled ? autostart::AutostartEntry::Enable(error) : autostart::AutostartEntry::Disable(error);
        return Report(L"autostart", applied ? Status::Ok() : Status::Failed(error));
    }

    Status SetShowDriverName(bool enabled)
    {
        Operation operation;
        if (!operation.Locked()) return Status::Failed(L"management-busy");
        const bool applied = settings::WriteBool(settings::kShowDriverName, enabled) &&
            settings::ReadBool(settings::kShowDriverName, !enabled) == enabled;
        return Report(L"show-driver-name", applied ? Status::Ok() : Status::Failed(L"settings-write"));
    }

    Status RegisterStartMenu()
    {
        Operation operation;
        if (!operation.Locked()) return Status::Failed(L"management-busy");
        std::wstring error;
        const bool applied = startmenu::StartMenuEntry::Register(error);
        return Report(L"startmenu", applied ? Status::Ok() : Status::Failed(error));
    }

    Status Uninstall()
    {
        Operation operation;
        if (!operation.Locked()) return Status::Failed(L"management-busy");
        struct Actions final : maintenance::IUninstallActions
        {
            Status DisableAutostart() override
            {
                std::wstring error;
                const bool applied = autostart::AutostartEntry::Disable(error);
                return Report(L"uninstall-autostart", applied ? Status::Ok() : Status::Failed(error));
            }
            Status StopHost() override { return Report(L"uninstall-stop", StopUnlocked()); }
            Status ResetAudioPreferences() override
            {
                const auto restored = recording::RestoreCableEndpoints();
                if (!restored) return Report(L"uninstall-endpoints", restored);
                return Report(L"uninstall-audio", App::Instance().EndpointPolicy().ClearAppDefaultDevices());
            }
            Status RemoveSettings() override
            {
                return Report(L"uninstall-settings", settings::RemoveAll() ? Status::Ok() : Status::Failed(L"settings-delete"));
            }
            Status RemoveConfig() override
            {
                return Report(L"uninstall-config", RemovePath(platform::GetInstallDirectory() / L"vmex.ini", false));
            }
            Status CloseLogsAndRemoveCache() override
            {
                Report(L"uninstall-cleanup", Status::Ok());
                log::Logger::Instance().Flush();
                log::Logger::Instance().ClearSinks();
                return RemovePath(platform::GetCacheDirectory(), true);
            }
        } actions;
        return maintenance::RunUninstall(actions);
    }
}
