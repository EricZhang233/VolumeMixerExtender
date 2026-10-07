#include "InjectionService.h"

#include "InjectionContract.h"
#include "Logger.h"
#include "Platform.h"
#include "Strings.h"

#include <windows.h>
#include <tlhelp32.h>

#include <optional>
#include <string>

namespace vmex::inject
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"inject";
        constexpr wchar_t kTargetProcessExe[] = L"ShellHost.exe";
        constexpr wchar_t kKernelModule[] = L"kernel32.dll";
        constexpr unsigned long kTapProbeIntervalMs = 100;

        struct RemoteModule final
        {
            void* base = nullptr;
            std::wstring path;
            std::uint32_t error = 0;
        };

        RemoteModule FindRemoteModule(std::uint32_t processId, const wchar_t* moduleName)
        {
            RemoteModule result;
            const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
            if (snapshot == INVALID_HANDLE_VALUE)
            {
                result.error = ::GetLastError();
                return result;
            }

            MODULEENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (::Module32FirstW(snapshot, &entry) != FALSE)
            {
                do
                {
                    if (strings::EqualsIgnoreCase(entry.szModule, moduleName))
                    {
                        result.base = entry.modBaseAddr;
                        result.path = entry.szExePath;
                        break;
                    }
                } while (::Module32NextW(snapshot, &entry) != FALSE);
            }

            ::CloseHandle(snapshot);
            return result;
        }

        bool IsModuleLoaded(std::uint32_t processId, const std::wstring& moduleName)
        {
            return FindRemoteModule(processId, moduleName.c_str()).base != nullptr;
        }

        bool WaitForModule(std::uint32_t processId, const std::wstring& moduleName, std::uint32_t timeoutMs)
        {
            const ULONGLONG deadline = ::GetTickCount64() + timeoutMs;
            for (;;)
            {
                if (IsModuleLoaded(processId, moduleName))
                {
                    return true;
                }
                if (::GetTickCount64() >= deadline)
                {
                    return false;
                }
                ::Sleep(kTapProbeIntervalMs);
            }
        }

        std::optional<TargetProcess> FindShellHost()
        {
            const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
            {
                return std::nullopt;
            }

            const std::uint32_t sessionId = platform::GetCurrentSessionId();
            std::optional<TargetProcess> found;

            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (::Process32FirstW(snapshot, &entry) != FALSE)
            {
                do
                {
                    if (!strings::EqualsIgnoreCase(entry.szExeFile, kTargetProcessExe))
                    {
                        continue;
                    }

                    DWORD session = 0;
                    if (::ProcessIdToSessionId(entry.th32ProcessID, &session) == FALSE || session != sessionId)
                    {
                        continue;
                    }

                    TargetProcess target;
                    target.processId = entry.th32ProcessID;
                    target.imageName = entry.szExeFile;
                    found = target;
                    break;
                } while (::Process32NextW(snapshot, &entry) != FALSE);
            }

            ::CloseHandle(snapshot);
            return found;
        }

        Status InjectInto(std::uint32_t processId, const std::filesystem::path& module, std::uint32_t timeoutMs, void*& remoteModule)
        {
            wchar_t full[MAX_PATH * 4] = {};
            if (::GetFullPathNameW(module.c_str(), ARRAYSIZE(full), full, nullptr) == 0 ||
                ::GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES)
            {
                return Status::FromLastError(L"inject-path");
            }

            const DWORD access = PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                 PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | SYNCHRONIZE;
            const HANDLE process = ::OpenProcess(access, FALSE, processId);
            if (process == nullptr)
            {
                return Status::FromLastError(L"inject-open");
            }

            const std::size_t bytes = (std::wcslen(full) + 1) * sizeof(wchar_t);
            void* remote = ::VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (remote == nullptr)
            {
                const auto status = Status::FromLastError(L"inject-alloc");
                ::CloseHandle(process);
                return status;
            }

            SIZE_T written = 0;
            if (::WriteProcessMemory(process, remote, full, bytes, &written) == FALSE)
            {
                const auto status = Status::FromLastError(L"inject-write");
                ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
                ::CloseHandle(process);
                return status;
            }

            const RemoteModule kernel = FindRemoteModule(processId, kKernelModule);
            const HMODULE localKernel = ::GetModuleHandleW(kKernelModule);
            const FARPROC localLoad = localKernel == nullptr ? nullptr : ::GetProcAddress(localKernel, "LoadLibraryW");
            if (kernel.base == nullptr || localLoad == nullptr)
            {
                ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
                ::CloseHandle(process);
                return Status::Failed(L"inject-resolver");
            }

            const auto offset = reinterpret_cast<std::uintptr_t>(localLoad) -
                                reinterpret_cast<std::uintptr_t>(localKernel);
            const auto remoteLoad = reinterpret_cast<LPTHREAD_START_ROUTINE>(
                reinterpret_cast<std::uintptr_t>(kernel.base) + offset);

            const HANDLE thread = ::CreateRemoteThread(process, nullptr, 0, remoteLoad, remote, 0, nullptr);
            if (thread == nullptr)
            {
                const auto status = Status::FromLastError(L"inject-thread");
                ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
                ::CloseHandle(process);
                return status;
            }

            Status status = Status::Ok();
            if (::WaitForSingleObject(thread, timeoutMs) != WAIT_OBJECT_0)
            {
                status = Status::Failed(L"inject-timeout");
            }
            else
            {
                DWORD exitCode = 0;
                if (::GetExitCodeThread(thread, &exitCode) == FALSE || exitCode == 0)
                {
                    status = Status::Failed(L"inject-load-failed");
                }
                else
                {
                    remoteModule = reinterpret_cast<void*>(static_cast<std::uintptr_t>(exitCode));
                }
            }

            ::CloseHandle(thread);
            ::VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            ::CloseHandle(process);
            return status;
        }

        class WindowsInjectionService final : public IInjectionService
        {
        public:
            Status FindTarget(TargetProcess& target) override
            {
                log::Logger::Instance().WriteKey(log::Level::Debug, kChannel, L"log.inject.find_target");

                const auto found = FindShellHost();
                if (!found.has_value())
                {
                    log::Logger::Instance().WriteKey(log::Level::Warn, kChannel, L"log.inject.target_missing");
                    target = {};
                    return Status::Failed(L"target-missing");
                }

                target = *found;
                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Info,
                    kChannel,
                    L"log.inject.target_found",
                    { std::to_wstring(target.processId), target.imageName });
                return Status::Ok();
            }

            Status Inject(const Options& options, State& state) override
            {
                TargetProcess target;
                const auto targetStatus = FindTarget(target);
                if (!targetStatus.IsOk())
                {
                    state = {};
                    return targetStatus;
                }

                if (!platform::FileExists(options.launcherModule))
                {
                    log::Logger::Instance().WriteKeyFormat(
                        log::Level::Error,
                        kChannel,
                        L"log.inject.launcher_missing",
                        { options.launcherModule.wstring() });
                    state = {};
                    return Status::Failed(L"launcher-missing");
                }

                const std::wstring tapName(kTapDllName);

                if (IsModuleLoaded(target.processId, tapName))
                {
                    const RemoteModule loaded = FindRemoteModule(target.processId, tapName.c_str());
                    if (!loaded.path.empty() &&
                        !strings::EqualsIgnoreCase(loaded.path, options.tapModule.wstring()))
                    {
                        log::Logger::Instance().WriteKeyFormat(
                            log::Level::Warn,
                            kChannel,
                            L"log.inject.stale_tap",
                            { loaded.path, options.tapModule.wstring() });
                    }
                    else
                    {
                        log::Logger::Instance().WriteKeyFormat(
                            log::Level::Info,
                            kChannel,
                            L"log.inject.already_loaded",
                            { std::to_wstring(target.processId) });
                    }

                    state.target = target;
                    state.launcherLoaded = IsModuleLoaded(target.processId, std::wstring(kLauncherDllName));
                    state.tapLoaded = true;
                    return Status::Ok();
                }

                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Info,
                    kChannel,
                    L"log.inject.payload",
                    { options.launcherModule.wstring(), options.tapModule.wstring() });

                const std::wstring coreName(kCoreDllName);
                if (!IsModuleLoaded(target.processId, coreName))
                {
                    void* coreBase = nullptr;
                    const auto coreStatus = InjectInto(target.processId, options.coreModule, options.timeoutMs, coreBase);
                    if (!coreStatus.IsOk())
                    {
                        log::Logger::Instance().WriteKeyFormat(
                            log::Level::Error,
                            kChannel,
                            L"log.inject.core_failed",
                            { std::to_wstring(target.processId), coreStatus.detail });
                        state = {};
                        return coreStatus;
                    }
                    log::Logger::Instance().WriteKeyFormat(
                        log::Level::Info,
                        kChannel,
                        L"log.inject.core_loaded",
                        { std::to_wstring(target.processId) });
                }

                void* remoteModule = nullptr;
                const auto status = InjectInto(target.processId, options.launcherModule, options.timeoutMs, remoteModule);
                if (!status.IsOk())
                {
                    log::Logger::Instance().WriteKeyFormat(
                        log::Level::Error,
                        kChannel,
                        L"log.inject.failed",
                        { std::to_wstring(target.processId), status.detail });
                    state = {};
                    return status;
                }

                const bool tapLoaded = WaitForModule(target.processId, tapName, options.timeoutMs);
                log::Logger::Instance().WriteKeyFormat(
                    tapLoaded ? log::Level::Info : log::Level::Warn,
                    kChannel,
                    tapLoaded ? L"log.inject.tap_ready" : L"log.inject.tap_pending",
                    { std::to_wstring(target.processId) });

                state.target = target;
                state.launcherLoaded = true;
                state.tapLoaded = tapLoaded;
                ++state.injectionCount;
                return Status::Ok();
            }

            Status QueryState(State& state) override
            {
                state = {};

                const auto found = FindShellHost();
                if (!found.has_value())
                {
                    return Status::Ok();
                }

                state.target = *found;
                state.launcherLoaded = IsModuleLoaded(found->processId, std::wstring(kLauncherDllName));
                state.tapLoaded = IsModuleLoaded(found->processId, std::wstring(kTapDllName));
                return Status::Ok();
            }
        };
    }

    std::unique_ptr<IInjectionService> CreateInjectionService()
    {
        return std::make_unique<WindowsInjectionService>();
    }
}
