#include "MaintenanceWorkflow.h"
#include "ConfigService.h"
#include "TapCommand.h"
#include "TapPipeServer.h"
#include "TextService.h"
#include "Platform.h"
#include "AudioInterop.h"
#include "RecordingMode.h"
#include "RecordingBackend.h"
#include "AudioFrameQueue.h"
#include "RecordingClient.h"
#include "ManagementService.h"
#include "RecordingDriverService.h"
#include "EndpointVisibility.h"
#include "AudioRoutingCleanup.h"
#include "AboutService.h"
#include "RecordingGuard.h"
#include "Strings.h"
#include "RecordingEndpointLock.h"
#include "PanelPresentation.h"

#include <windows.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <fstream>

namespace
{
    void Require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    struct UninstallProbe final : vmex::maintenance::IUninstallActions
    {
        int failAt = -1;
        int step = 0;
        bool running = true;
        bool logsOpen = true;
        bool settingsPresent = true;
        bool configPresent = true;
        bool cachePresent = true;
        bool autostart = true;
        bool audioPresent = true;
        vmex::Status Next()
        {
            return step++ == failAt ? vmex::Status::Failed(L"injected-failure") : vmex::Status::Ok();
        }
        vmex::Status DisableAutostart() override
        {
            auto result = Next();
            if (result) autostart = false;
            return result;
        }
        vmex::Status StopHost() override
        {
            auto result = Next();
            if (result) running = false;
            return result;
        }
        vmex::Status ResetAudioPreferences() override
        {
            Require(!running, "Audio cleanup must not race a live host");
            auto result = Next();
            if (result) audioPresent = false;
            return result;
        }
        vmex::Status RemoveSettings() override
        {
            Require(!audioPresent, "Failed audio cleanup must not silently destroy configuration");
            auto result = Next();
            if (result) settingsPresent = false;
            return result;
        }
        vmex::Status RemoveConfig() override
        {
            auto result = Next();
            if (result) configPresent = false;
            return result;
        }
        vmex::Status CloseLogsAndRemoveCache() override
        {
            Require(!running && !settingsPresent && !configPresent, "Cache cleanup must be last");
            auto result = Next();
            if (result)
            {
                logsOpen = false;
                cachePresent = false;
            }
            return result;
        }
    };

    void UninstallFailures()
    {
        for (int fail = 0; fail < 6; ++fail)
        {
            UninstallProbe probe;
            probe.failAt = fail;
            const auto result = vmex::maintenance::RunUninstall(probe);
            Require(!result, "A failed uninstall must not report success");
            Require(probe.step == fail + 1, "A failed stage must prevent later cleanup");
            Require(probe.cachePresent, "Failed uninstall must preserve cached diagnostics");
            if (fail <= 1) Require(probe.audioPresent && probe.settingsPresent, "Stop failure must preserve audio and settings");
        }
        UninstallProbe success;
        Require(vmex::maintenance::RunUninstall(success).IsOk(), "Uninstall should complete");
        Require(!success.running && !success.autostart && !success.audioPresent && !success.settingsPresent &&
                !success.configPresent && !success.cachePresent && !success.logsOpen, "Uninstall must clean all state");
    }

    void AudioSessionReset()
    {
        struct Volume final : ISimpleAudioVolume
        {
            float level = 0.23f;
            BOOL muted = TRUE;
            bool reject = false;
            bool ignore = false;
            bool unreadable = false;
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override { *object = nullptr; return E_NOINTERFACE; }
            ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
            ULONG STDMETHODCALLTYPE Release() override { return 1; }
            HRESULT STDMETHODCALLTYPE SetMasterVolume(float value, LPCGUID) override { if (reject) return E_FAIL; if (!ignore) level = value; return S_OK; }
            HRESULT STDMETHODCALLTYPE GetMasterVolume(float* value) override { if (unreadable) return E_FAIL; *value = level; return S_OK; }
            HRESULT STDMETHODCALLTYPE SetMute(BOOL value, LPCGUID) override { muted = value; return S_OK; }
            HRESULT STDMETHODCALLTYPE GetMute(BOOL* value) override { *value = muted; return S_OK; }
        } app, system, inactive, expired;
        std::vector<vmex::audio::detail::Session> sessions(4);
        Volume* controls[] = { &app, &system, &inactive, &expired };
        for (std::size_t i = 0; i < sessions.size(); ++i)
        {
            *sessions[i].vol.put() = controls[i];
            sessions[i].state = AudioSessionStateActive;
        }
        sessions[1].systemSounds = true;
        sessions[2].state = AudioSessionStateInactive;
        sessions[3].state = AudioSessionStateExpired;
        Require(vmex::audio::detail::ResetSessionVolumes(L"", sessions), "Session volume reset failed");
        Require(app.level == 1.0f && system.level == 1.0f && inactive.level == 1.0f,
                "Reset must include ordinary, system sounds and inactive sessions");
        Require(expired.level == 0.23f && app.muted && system.muted, "Reset must preserve mute and skip expired sessions");
        app.reject = true; system.level = 0.4f;
        Require(!vmex::audio::detail::ResetSessionVolumes(L"", sessions), "A failed volume reset must be reported");
        Require(system.level == 1.0f, "One failed session must not prevent other volume resets");
        app.reject = false; app.ignore = true; app.level = 0.23f; system.level = 0.4f;
        Require(!vmex::audio::detail::ResetSessionVolumes(L"", sessions), "An unchanged volume must not report success");
        Require(system.level == 1.0f, "Readback mismatch must not prevent other volume resets");
        app.ignore = false; app.unreadable = true; system.level = 0.4f;
        Require(!vmex::audio::detail::ResetSessionVolumes(L"", sessions), "An unreadable volume must not report success");
        Require(system.level == 1.0f, "Readback failure must not prevent other volume resets");
    }

    void AudioLiveReset()
    {
        using namespace vmex::audio::detail;
        Require(SUCCEEDED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "Audio COM initialization failed");
        {
            auto enumerator = MakeEnumerator();
            Ptr<IMMDevice> device;
            Require(enumerator && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put())), "No audio endpoint for live test");
            auto manager = SessionManager(device.get());
            Require(static_cast<bool>(manager), "Audio session manager unavailable");
            GUID sessionId{};
            Require(SUCCEEDED(::CoCreateGuid(&sessionId)), "Test session identifier unavailable");
            std::vector<Session> sessions(1);
            auto& session = sessions.front();
            Require(SUCCEEDED(manager->GetSimpleAudioVolume(&sessionId, 0, session.vol.put())), "Test volume interface unavailable");
            Require(SUCCEEDED(session.vol->SetMasterVolume(0.23f, nullptr)), "Test volume adjustment failed");
            session.state = AudioSessionStateInactive;
            Require(ResetSessionVolumes(L"", sessions), "Live session reset failed");
            float actual = 0.0f;
            Require(SUCCEEDED(session.vol->GetMasterVolume(&actual)) && actual >= 0.9999f, "Live session did not return to full volume");
        }
        ::CoUninitialize();
    }

    void AudioFailures()
    {
        AudioSessionReset();
        struct Probe final : vmex::maintenance::IAudioResetActions
        {
            int failMask = 0;
            int calledMask = 0;
            vmex::Status Step(int bit)
            {
                calledMask |= bit;
                return failMask & bit ? vmex::Status::Failed(std::to_wstring(bit)) : vmex::Status::Ok();
            }
            vmex::Status ClearRouting() override { return Step(1); }
            vmex::Status ResetSessionVolumes() override { return Step(2); }
            vmex::Status ClearPersistedVolumes() override { return Step(4); }
        };
        for (int mask = 0; mask < 8; ++mask)
        {
            Probe probe;
            probe.failMask = mask;
            const auto result = vmex::maintenance::ResetAudioPreferences(probe);
            Require(result.IsOk() == (mask == 0), "Partial audio reset must not report full success");
            Require(probe.calledMask == 7, "Volume reset must still run when routing reset fails");
            for (const int bit : { 1, 2, 4 })
                if (mask & bit) Require(result.detail.find(std::to_wstring(bit)) != std::wstring::npos,
                                       "All audio reset failures must survive aggregation");
        }
    }

    void GuardParent(const char* filename)
    {
        const std::string name(filename);
        Require(!name.empty() && name.find_first_not_of("0123456789.VmexGuardCrashpid") == std::string::npos,
            "Guard fixture signal must be a temporary filename");
        vmex::recording::Guard guard;
        const auto started = guard.Start({ L"fixture-console", L"fixture-multimedia" }, L"fixture-virtual");
        Require(started.IsOk(), "Fixture guard must start");
        std::ofstream signal(std::filesystem::temp_directory_path() / vmex::strings::ToWide(name));
        signal << ::GetProcessId(guard.process);
        signal.close();
        Require(static_cast<bool>(signal), "Fixture guard signal must be written");
        ::Sleep(INFINITE);
    }

    void GuardRecovery()
    {
        struct Probe final : vmex::recording::IRecoveryActions
        {
            std::string calls;
            int stage = 0, remaining = 0;
            vmex::Status Step(char call, int current)
            {
                calls += call;
                if (stage == current && remaining != 0)
                {
                    if (remaining > 0) --remaining;
                    return vmex::Status::Failed(L"recovery-injected");
                }
                return vmex::Status::Ok();
            }
            vmex::Status RestoreDefaults() override { return Step('R', 1); }
            vmex::Status ClearRedirects() override { return Step('C', 2); }
            vmex::Status DisableEndpoint() override { return Step('V', 3); }
        } probe;
        Require(vmex::recording::RecoverAfterHostExit(probe).IsOk() && probe.calls == "RCV",
            "Crash recovery must restore defaults, clear routes, then disable the endpoint");
        for (int stage = 1; stage <= 3; ++stage)
        {
            probe = {}; probe.stage = stage; probe.remaining = 1;
            const std::string expected[]{ "", "RRCV", "RCRCV", "RCVRCV" };
            Require(vmex::recording::RecoverAfterHostExit(probe).IsOk() && probe.calls == expected[stage],
                "Transient recovery failures must retry the full ordered sequence");
            probe = {}; probe.stage = stage; probe.remaining = -1;
            const std::string failed[]{ "", "RRR", "RCRCRC", "RCVRCVRCV" };
            Require(!vmex::recording::RecoverAfterHostExit(probe) && probe.calls == failed[stage],
                "Persistent recovery failures must stop after bounded retries without skipping prerequisites");
        }
        struct Children final
        {
            PROCESS_INFORMATION parent{};
            HANDLE guardian = nullptr;
            std::filesystem::path signal;
            ~Children()
            {
                if (parent.hProcess)
                {
                    if (::WaitForSingleObject(parent.hProcess, 0) == WAIT_TIMEOUT) ::TerminateProcess(parent.hProcess, 1);
                    ::WaitForSingleObject(parent.hProcess, 1000);
                    ::CloseHandle(parent.hThread); ::CloseHandle(parent.hProcess);
                }
                if (guardian)
                {
                    if (::WaitForSingleObject(guardian, 1000) == WAIT_TIMEOUT) ::TerminateProcess(guardian, 1);
                    ::CloseHandle(guardian);
                }
                std::error_code error;
                std::filesystem::remove(signal, error);
            }
        } children;
        const auto name = L"VmexGuardCrash." + std::to_wstring(::GetCurrentProcessId()) + L"." + std::to_wstring(::GetTickCount64()) + L".pid";
        children.signal = std::filesystem::temp_directory_path() / name;
        const auto executable = vmex::platform::GetExecutablePath();
        std::wstring command = L"\"" + executable.wstring() + L"\" guard-parent " + name;
        STARTUPINFOW startup{ sizeof(startup) };
        Require(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &children.parent) != FALSE, "Fixture parent must start without a visible window");
        DWORD guardianId = 0;
        const auto deadline = ::GetTickCount64() + 5000;
        while (::GetTickCount64() < deadline && !guardianId)
        {
            std::ifstream signal(children.signal);
            signal >> guardianId;
            if (!guardianId) ::Sleep(10);
        }
        Require(guardianId != 0, "Fixture parent must publish its ready guardian");
        children.guardian = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, guardianId);
        Require(children.guardian != nullptr, "Fixture guardian must be observable");
        Require(::TerminateProcess(children.parent.hProcess, 1) != FALSE, "Fixture parent must be terminated to simulate a crash");
        Require(::WaitForSingleObject(children.guardian, 8000) == WAIT_OBJECT_0,
            "Guardian must notice parent death and finish bounded recovery");
        DWORD code = STILL_ACTIVE;
        Require(::GetExitCodeProcess(children.guardian, &code) && code == vmex::kCodeFailed,
            "Missing fixture audio endpoint must produce an honest recovery failure, not false success");
    }

    void EndpointLockPolicy()
    {
        struct Actions final : vmex::recording::IEndpointLockActions
        {
            float level = 1.0f;
            std::array<std::wstring, 2> defaults{L"V", L"V"};
            unsigned writes = 0;
            bool failRead = false, failVolume = false;
            int failDefaultRead = -1, failDefaultWrite = -1;
            vmex::Status ReadVolume(float& value) override
            { value = level; return failRead ? vmex::Status::Failed(L"read-volume") : vmex::Status::Ok(); }
            vmex::Status SetVolume(float value) override
            {
                ++writes;
                if (failVolume) return vmex::Status::Failed(L"write-volume");
                level = value;
                return vmex::Status::Ok();
            }
            vmex::Status ReadDefault(unsigned role, std::wstring& endpoint) override
            {
                Require(role < defaults.size(), "Recording lock must not touch capture or communications roles");
                endpoint = defaults[role];
                return static_cast<int>(role) == failDefaultRead ? vmex::Status::Failed(L"read-default") : vmex::Status::Ok();
            }
            vmex::Status SetDefault(unsigned role, std::wstring_view endpoint) override
            {
                ++writes;
                if (static_cast<int>(role) == failDefaultWrite) return vmex::Status::Failed(L"write-default");
                defaults[role] = endpoint;
                return vmex::Status::Ok();
            }
        } actions;
        using vmex::recording::EnforceRecordingEndpoint;
        Require(EnforceRecordingEndpoint(actions, L"V").IsOk() && actions.writes == 0,
            "A stable lock must not repeatedly write volume or default output");
        actions.level = 0.2f;
        actions.defaults = {L"R1", L"R2"};
        Require(EnforceRecordingEndpoint(actions, L"V").IsOk() && actions.level == 1.0f &&
            actions.defaults[0] == L"V" && actions.defaults[1] == L"V" && actions.writes == 3,
            "External volume and default changes must be corrected together");
        Require(EnforceRecordingEndpoint(actions, L"V").IsOk() && actions.writes == 3,
            "Self-generated notifications must settle without a write loop");
        actions.failDefaultRead = 1;
        Require(EnforceRecordingEndpoint(actions, L"V").IsOk() && actions.writes == 4,
            "A temporarily missing default must be recovered to V");
        for (unsigned failure = 0; failure < 4; ++failure)
        {
            Actions broken;
            broken.level = 0.1f;
            broken.defaults = {L"R1", L"R2"};
            broken.failRead = failure == 0;
            broken.failVolume = failure == 1;
            broken.failDefaultWrite = failure >= 2 ? static_cast<int>(failure - 2) : -1;
            Require(!EnforceRecordingEndpoint(broken, L"V").IsOk(), "Every lock failure must propagate to recording health");
            if (failure < 2) Require(broken.defaults[0] == L"R1", "Volume failure must not silently rewrite defaults");
        }
        Actions empty;
        Require(!EnforceRecordingEndpoint(empty, L"").IsOk() && empty.writes == 0, "An empty endpoint must never mutate policy");
        vmex::recording::RecordingEndpointLock lock;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            Require(!lock.Start(L"missing-fixture-endpoint").IsOk(), "Missing hardware must fail without creating a live lock");
            lock.Stop();
            lock.Stop();
        }
    }

    void Presentation()
    {
        using namespace vmex::presentation;
        DeviceSnapshotBuffer buffer;
        DeviceSnapshot before;
        before.render.resize(1);
        before.render[0].id = L"R1";
        before.defaultRender = before.appsEndpoint = L"R1";
        auto recording = before;
        recording.recordingMode = true;
        recording.defaultRender = recording.appsEndpoint = L"V";
        buffer.Hold(100, 800);
        buffer.Hold(200);
        Require(buffer.Waiting(899) && !buffer.Observe(recording, 899),
            "A delayed device notification must not shorten recording transition buffering");
        Require(!buffer.Observe(recording, 900) && !buffer.Observe(recording, 1149) && buffer.Observe(recording, 1150),
            "Recording completion must wait for matching snapshots across the stability interval");
        buffer.Hold(1200);
        DeviceSnapshot empty;
        Require(!buffer.Observe(empty, 1600) && !buffer.Observe(before, 1750) && !buffer.Observe(before, 1999) &&
            buffer.Observe(before, 2000), "A transient empty device list must be discarded while the old display remains mounted");
        auto movedDefault = before;
        movedDefault.defaultRender = movedDefault.appsEndpoint = L"R2";
        Require(!buffer.Observe(movedDefault, 2050) && buffer.Observe(movedDefault, 2300),
            "Default changes must restart snapshot stabilization even when device membership is unchanged");
        buffer.Hold(2400);
        Require(!buffer.Observe(movedDefault, 2800), "A late notification must invalidate an earlier stable sample");
        buffer.Hold(2900);
        Require(!buffer.Observe(movedDefault, 3150) && !buffer.Observe(movedDefault, 3300) && buffer.Observe(movedDefault, 3550),
            "Notification bursts must merge into one stable refresh after the final quiet interval");
        auto routingOnly = before;
        routingOnly.routingRender = recording.render;
        Require(SameVolumeRows(before, routingOnly) && !SameDeviceSnapshot(before, routingOnly),
            "Routing changes must update applications while preserving endpoint controls and subscriptions");
        Require(SameVolumeRows(before, recording) && !SameDeviceSnapshot(before, recording),
            "Entering and exiting recording with unchanged physical devices must reuse controls while updating the mode and applications");
        auto renamed = before;
        renamed.render[0].friendlyName = L"new name";
        Require(!SameVolumeRows(before, renamed), "Visible device changes must still rebuild endpoint controls");
        vmex::presentation::PendingSelection<bool> toggle;
        Require(!toggle.Take(100), "Idle polling must not manufacture a toggle action");
        toggle.Queue(true, 100);
        toggle.Queue(false, 150);
        Require(toggle && !toggle.Due(349) && !toggle.Take(349), "Rapid toggles must wait for the last intent's quiet interval");
        const auto off = toggle.Take(350);
        Require(off.has_value() && !*off && !toggle, "A queued off action must survive debounce as a real value");
        toggle.Queue(true, 400);
        Require(toggle.ValueOr(false) && toggle.Take(900).value_or(false) && !toggle.Take(901),
            "A busy status poll may delay but must not duplicate or lose the user's action");
        toggle.Queue(true, 1000);
        toggle.Reset();
        Require(!toggle.Take(2000), "Leaving the page must cancel pending clicks");
        vmex::presentation::PendingSelection<std::wstring> selection;
        selection.Queue(L"R1", 0);
        selection.Queue(L"R2", 100);
        Require(!selection.Take(299) && selection.Take(300).value_or(L"") == L"R2", "Only the last device click must be applied");
        std::vector<vmex::audio::DeviceInfo> devices(3);
        devices[0].id = L"R1"; devices[1].id = L"R2"; devices[2].id = L"R3";
        vmex::presentation::PrioritizeMonitor(devices, L"R2");
        Require(devices[0].id == L"R2" && devices[0].isDefault && devices[1].id == L"R1" && devices[2].id == L"R3",
            "R must be selected and first while other devices keep their relative order");
        auto repeated = devices;
        vmex::presentation::PrioritizeMonitor(repeated, L"R2");
        Require(vmex::presentation::SameDeviceRows(devices, repeated), "Repeated device notifications must not require reconstruction");
        repeated[0].friendlyName = L"Changed";
        Require(!vmex::presentation::SameDeviceRows(devices, repeated), "A visible device rename must refresh the list");
        repeated = devices;
        repeated[0].isDefault = false;
        Require(!vmex::presentation::SameDeviceRows(devices, repeated), "A changed selected device must refresh the highlight");
        vmex::presentation::PrioritizeMonitor(devices, L"missing");
        Require(std::none_of(devices.begin(), devices.end(), [](const auto& device) { return device.isDefault; }),
            "An unplugged monitor must not highlight a different device as R");
    }

    void Recording()
    {
        using namespace vmex::recording;
        DefaultOutputMemory outputs;
        const std::array<std::wstring, 2> original{L"console-before", L"R1"};
        outputs.Remember(original);
        Require(outputs.Monitor() == L"R1", "Entry monitor must use the previous multimedia default");
        Require(outputs.Targets() == original, "Failed startup must preserve both original default roles");
        outputs.SelectMonitor(outputs.Monitor());
        Require(outputs.Targets() == std::array<std::wstring, 2>{L"R1", L"R1"},
            "Normal exit without a monitor change must select the initial monitor for both roles");
        outputs.SelectMonitor(L"R2");
        Require(outputs.Targets() == std::array<std::wstring, 2>{L"R2", L"R2"},
            "Exit after a monitor change must use the latest monitor instead of entry defaults");
        outputs.SelectMonitor(L"R3");
        Require(outputs.Targets() == std::array<std::wstring, 2>{L"R3", L"R3"},
            "Repeated changes must retain the last successful monitor");
        outputs.Remember({L"next-console", L"R3"});
        Require(outputs.Monitor() == L"R3" && outputs.Targets()[0] == L"next-console",
            "A new recording session must refresh defaults and reset startup rollback state");
        struct VisibilityProbe final : IVisibilityActions
        {
            std::vector<VisibilityState> endpoints;
            std::wstring reject;
            bool rejectStore = false;
            vmex::Status Enumerate(std::vector<VisibilityState>& values) override { values = endpoints; return vmex::Status::Ok(); }
            vmex::Status Store(std::wstring_view id, bool owned) override
            {
                if (rejectStore) return vmex::Status::Failed(L"store");
                for (auto& endpoint : endpoints) if (endpoint.id == id) endpoint.owned = owned;
                return vmex::Status::Ok();
            }
            vmex::Status SetVisible(std::wstring_view id, bool visible) override
            {
                if (id == reject) return vmex::Status::Failed(L"visibility");
                for (auto& endpoint : endpoints) if (endpoint.id == id) endpoint.visible = visible;
                return vmex::Status::Ok();
            }
        } visibility;
        Require(IsCableHardwareId(L"root\\vbaudiovacwdm") && IsCableHardwareId(L"VBAudioVACWDM"), "Official hardware IDs must match case insensitively");
        Require(!IsCableHardwareId(L"VBAudioVACWDM_A") && !IsCableHardwareId(L"OtherVBAudioVACWDM") &&
            !IsCableHardwareId(L"ROOT\\VBAudioVACWDM\\0000"), "Substring and instance IDs must not count as hardware identity");
        visibility.endpoints = { {L"user-disabled", false, false}, {L"active", true, false}, {L"previously-hidden", false, true} };
        std::vector<VisibilityState> changes;
        Require(ChangeVisibility(visibility, false, changes).IsOk() && changes.size() == 1 && visibility.endpoints[1].owned,
            "Hide must claim only endpoints actually changed by the program");
        changes.clear();
        Require(ChangeVisibility(visibility, true, changes).IsOk() && !visibility.endpoints[0].visible &&
            visibility.endpoints[1].visible && visibility.endpoints[2].visible, "Show must preserve endpoints originally disabled by the user");
        Require(RestoreVisibility(visibility, changes).IsOk() && !visibility.endpoints[1].visible && visibility.endpoints[1].owned &&
            !visibility.endpoints[2].visible && visibility.endpoints[2].owned, "Installer cancellation must restore visibility and ownership exactly");
        visibility.endpoints = { {L"first", true, false}, {L"second", true, false} };
        visibility.reject = L"second";
        changes.clear();
        Require(!ChangeVisibility(visibility, false, changes), "A partial visibility failure must be reported");
        visibility.reject.clear();
        Require(RestoreVisibility(visibility, changes).IsOk() && visibility.endpoints[0].visible && !visibility.endpoints[0].owned &&
            visibility.endpoints[1].visible && !visibility.endpoints[1].owned, "Rollback must undo a partially completed hide");
        visibility.rejectStore = true;
        changes.clear();
        Require(!ChangeVisibility(visibility, false, changes) && changes.empty() && visibility.endpoints[0].visible,
            "A failed ownership write must prevent changes to system visibility");
        visibility.rejectStore = false;
        visibility.endpoints = { {L"blocked", true, false} };
        visibility.reject = L"blocked";
        changes.clear();
        Require(!ChangeVisibility(visibility, false, changes) && !RestoreVisibility(visibility, changes) && visibility.endpoints[0].owned,
            "Failed rollback must retain the ownership record for a later recovery attempt");
        Require(vmex::management::IsHostImage(L"C:\\App\\vmex.exe", L"C:\\App") &&
            vmex::management::IsHostImage(L"C:\\APP\\vmex_cli.exe", L"c:\\app"), "Both same-directory host entry points must be recognized");
        Require(!vmex::management::IsHostImage(L"C:\\Other\\vmex.exe", L"C:\\App") &&
            !vmex::management::IsHostImage(L"C:\\App\\unrelated.exe", L"C:\\App"), "Another copy or unrelated executable must be rejected");
        const auto registryPath = L"Software\\EricSoft\\VolumeMixerExtender.Tests." + std::to_wstring(::GetCurrentProcessId());
        struct Registry final
        {
            HKEY key = nullptr;
            std::wstring path;
            ~Registry() { if (key) ::RegCloseKey(key); ::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); }
        } fixture;
        fixture.path = registryPath;
        Require(::RegCreateKeyExW(HKEY_CURRENT_USER, (registryPath + L"\\ClosedApp").c_str(), 0, nullptr, 0,
            KEY_ALL_ACCESS, nullptr, &fixture.key, nullptr) == ERROR_SUCCESS, "Routing fixture must be isolated");
        const std::wstring virtualId = L"{0.0.0.00000000}.{virtual-test}";
        const std::wstring physicalId = L"{0.0.0.00000000}.{physical-test}";
        const auto set = [&](const wchar_t* name, const std::wstring& value) {
            Require(::RegSetValueExW(fixture.key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS, "Routing fixture value failed");
        };
        set(L"000_000", vmex::audio::detail::PackDeviceId(virtualId, eRender));
        set(L"000_000_p", L"metadata");
        set(L"001_000", physicalId);
        set(L"001_000_p", L"preserved");
        set(L"", L"C:\\ClosedApp.exe");
        Require(vmex::recording::ClearEndpointPreferences(fixture.key, virtualId).IsOk(), "Exited application virtual routing must clear");
        Require(::RegQueryValueExW(fixture.key, L"000_000", nullptr, nullptr, nullptr, nullptr) == ERROR_FILE_NOT_FOUND &&
            ::RegQueryValueExW(fixture.key, L"000_000_p", nullptr, nullptr, nullptr, nullptr) == ERROR_FILE_NOT_FOUND,
            "Only the virtual route and its paired metadata must be removed");
        Require(::RegQueryValueExW(fixture.key, L"001_000", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS &&
            ::RegQueryValueExW(fixture.key, L"001_000_p", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS &&
            ::RegQueryValueExW(fixture.key, L"", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS,
            "Other endpoint routing and application identity must survive");
        vmex::audio::detail::Endpoint cable;
        cable.hardwareId = L"VBAudioVACWDM";
        cable.interfaceName = L"VB-Audio Virtual Cable";
        cable.friendly = L"Renamed by user";
        Require(vmex::audio::detail::IsOurVirtualDevice(cable), "User-renamed VB-CABLE must remain hidden");
        cable.hardwareId = L"VBAudioVACAWDM";
        Require(!vmex::audio::detail::IsOurVirtualDevice(cable), "Additional VB-Audio cables must not be treated as ours");
        vmex::audio::detail::Endpoint extra;
        extra.hardwareId = L"VBAudioVACWDM";
        extra.formFactor = LineLevel;
        extra.jackSubType = { 0xdff21fe3, 0xf70f, 0x11d0, { 0xb9, 0x17, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } };
        extra.flow = eRender;
        extra.interfaceName = L"VB-Audio Virtual Cable";
        extra.description = L"CABLE In 16 Ch";
        extra.friendly = L"CABLE In 16 Ch (VB-Audio Virtual Cable)";
        Require(vmex::audio::detail::IsExtraCableEndpoint(extra), "The 16-channel cable input must be treated as extra");
        extra.description = L"CABLE Input";
        extra.formFactor = Speakers;
        extra.jackSubType.Data1 = 0xdff21ce1;
        extra.friendly = L"CABLE Input (VB-Audio Virtual Cable)";
        Require(!vmex::audio::detail::IsExtraCableEndpoint(extra), "CABLE Input must be classified separately from the two permanently disabled extras");
        extra.state = DEVICE_STATE_DISABLED;
        Require(vmex::audio::detail::GetCableEndpointRole(extra) == vmex::audio::detail::CableEndpointRole::Playback,
            "A standby-disabled CABLE Input must still be identifiable for the recording toggle");
        extra.friendly = L"CABLE In 16 Ch (VB-Audio Virtual Cable)";
        Require(!vmex::audio::detail::IsExtraCableEndpoint(extra) &&
            vmex::audio::detail::GetCableEndpointRole(extra) == vmex::audio::detail::CableEndpointRole::Playback,
            "Renaming the primary endpoint must not classify it as a permanently disabled extra");
        extra.description.clear();
        extra.interfaceName.clear();
        Require(vmex::audio::detail::GetCableEndpointRole(extra) == vmex::audio::detail::CableEndpointRole::Playback,
            "Missing display names must not prevent hardware and connector identification");
        extra.description = L"扬声器";
        extra.friendly = L"扬声器 (VB-Audio Virtual Cable)";
        Require(vmex::audio::detail::GetCableEndpointRole(extra) == vmex::audio::detail::CableEndpointRole::Playback,
            "The laptop's localized Speakers endpoint must be recognized as the primary cable output");
        extra.formFactor = LineLevel;
        extra.jackSubType.Data1 = 0xdff21fe3;
        extra.description = L"CABLE In 16 Ch";
        extra.friendly = L"Renamed by user";
        Require(vmex::audio::detail::IsExtraCableEndpoint(extra), "Renaming an extra endpoint must not prevent identification");
        extra.description = L"CABLE In 16 Ch custom";
        Require(vmex::audio::detail::IsExtraCableEndpoint(extra), "Renaming the endpoint description must not change its hardware role");
        extra.description = L"CABLE Output";
        Require(vmex::audio::detail::GetCableEndpointRole(extra) == vmex::audio::detail::CableEndpointRole::MultichannelPlayback,
            "A capture display name must not override the render connector role");
        extra.flow = eCapture;
        extra.description = L"CABLE Output";
        extra.friendly = L"CABLE Output (VB-Audio Virtual Cable)";
        Require(vmex::audio::detail::IsExtraCableEndpoint(extra), "The cable recording side must be treated as extra");
        extra.interfaceName = L"VB-Audio Cable A";
        extra.hardwareId = L"VBAudioVACAWDM";
        Require(!vmex::audio::detail::IsExtraCableEndpoint(extra), "Additional VB-Audio cable hardware must not be hidden");
        extra.hardwareId.clear();
        extra.interfaceName = L"VB-Audio Virtual Cable";
        Require(!vmex::audio::detail::IsOurVirtualDevice(extra) &&
            vmex::audio::detail::GetCableEndpointRole(extra) == vmex::audio::detail::CableEndpointRole::Unknown,
            "Display names must never substitute for a hardware ID");
        extra.hardwareId = L"VBAudioVACWDM";
        extra.jackSubType = {};
        Require(vmex::audio::detail::GetCableEndpointRole(extra) == vmex::audio::detail::CableEndpointRole::Unknown,
            "Unknown connector metadata must not authorize endpoint changes");
        extra.jackSubType = { 0xdff21fe3, 0xf70f, 0x11d0, { 0xb9, 0x17, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } };
        extra.interfaceName = L"VB-Audio Virtual Cable";
        extra.flow = eRender;
        extra.description = L"CABLE In 16 Ch";
        extra.state = DEVICE_STATE_ACTIVE;
        Require(vmex::audio::detail::CableEndpointNeedsVisibilityChange(extra, false), "A visible cable endpoint must be hidden");
        Require(!vmex::audio::detail::CableEndpointNeedsVisibilityChange(extra, true), "A visible cable endpoint needs no restore");
        extra.state = DEVICE_STATE_DISABLED;
        Require(!vmex::audio::detail::CableEndpointNeedsVisibilityChange(extra, false), "An already hidden cable endpoint needs no repeated hide");
        Require(vmex::audio::detail::CableEndpointNeedsVisibilityChange(extra, true), "A disabled cable endpoint must be restored when requested");
        Require(vmex::audio::detail::EndpointVisibilityMatches(DEVICE_STATE_DISABLED, false) &&
            !vmex::audio::detail::EndpointVisibilityMatches(DEVICE_STATE_ACTIVE, false), "Disable must be confirmed by the disabled state");
        Require(vmex::audio::detail::EndpointVisibilityMatches(DEVICE_STATE_ACTIVE, true) &&
            vmex::audio::detail::EndpointVisibilityMatches(DEVICE_STATE_UNPLUGGED, true) &&
            !vmex::audio::detail::EndpointVisibilityMatches(DEVICE_STATE_DISABLED, true) &&
            !vmex::audio::detail::EndpointVisibilityMatches(DEVICE_STATE_NOTPRESENT, true), "Missing or disabled endpoints must not count as a successful enable");
        const auto uninitialized = vmex::management::Query();
        Require(!uninitialized.injected, "Management query without an initialized app must not report injection state");
        vmex::recording::Guard guard;
        Require(guard.Start({ L"fixture-console", L"fixture-multimedia" }, L"fixture-virtual").IsOk() && guard.Running(),
            "Recovery guard must be ready before audio routing can change");
        Require(guard.UpdateDefaults({L"fixture-monitor-2", L"fixture-monitor-2"}).IsOk() && guard.Running(),
            "A monitor change must update crash recovery without restarting the guard");
        Require(!guard.UpdateDefaults({L"", L"fixture-monitor-2"}).IsOk() && guard.Running(),
            "Invalid recovery targets must be rejected without stopping protection");
        HANDLE guardProcess = nullptr;
        Require(::DuplicateHandle(::GetCurrentProcess(), guard.process, ::GetCurrentProcess(), &guardProcess,
            SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0) != FALSE, "Guard exit must be observable");
        guard.Stop();
        DWORD guardExit = STILL_ACTIVE;
        Require(::WaitForSingleObject(guardProcess, 0) == WAIT_OBJECT_0 && ::GetExitCodeProcess(guardProcess, &guardExit) && guardExit == 0,
            "Normal recording shutdown must cancel and join its recovery guard");
        ::CloseHandle(guardProcess);
        struct Backend final : vmex::recording::IBackend
        {
            std::string calls;
            int fail = 0;
            bool failRestore = false;
            bool failDisconnect = false;
            bool mirrorFailed = false;
            bool throwFailure = false;
            vmex::Status Step(char name, int stage)
            {
                calls += name;
                if (fail == stage && throwFailure) throw std::runtime_error("injected");
                return fail == stage ? vmex::Status::Failed(L"injected") : vmex::Status::Ok();
            }
            vmex::Status SaveDefaults(std::wstring& monitor) override { monitor = L"speakers"; return Step('S', 1); }
            vmex::Status ConnectVirtualDevice() override { return Step('V', 2); }
            vmex::Status StartMirror(std::wstring_view monitor) override
            {
                Require(monitor == L"speakers", "Monitor must originate from the default output");
                return Step('M', 3);
            }
            vmex::Status SelectVirtualDevice() override { return Step('D', 4); }
            vmex::Status RestoreDefaults() override { calls += 'R'; return failRestore ? vmex::Status::Failed(L"restore") : vmex::Status::Ok(); }
            void StopMirror() noexcept override { calls += 'm'; }
            vmex::Status DisconnectVirtualDevice() override
            {
                const auto result = Step('v', 6);
                return failDisconnect ? vmex::Status::Failed(L"disable-endpoint") : result;
            }
            vmex::Status ChangeMonitor(std::wstring_view) override { return Step('C', 5); }
            vmex::Status MirrorState() const override { return mirrorFailed ? vmex::Status::Failed(L"device-lost") : vmex::Status::Ok(); }
        };
        for (int failure = 1; failure <= 4; ++failure)
        {
            Backend backend;
            backend.fail = failure;
            vmex::recording::Mode mode(backend);
            Require(!mode.Enable(), "A failed stage must not enable recording");
            Require(!mode.State().enabled && !mode.State().cleanupPending, "Successful rollback must leave recording off");
            const std::string expected[] = { "", "S", "SVRmv", "SVMRmv", "SVMDRmv" };
            Require(backend.calls == expected[failure], "Recording startup rollback order is unsafe");
        }
        Backend backend;
        vmex::recording::Mode mode(backend);
        Require(mode.Enable().IsOk() && backend.calls == "SVMD", "Default routing must change only after mirror startup");
        Require(mode.Enable().IsOk() && backend.calls == "SVMD", "Repeated enable must be idempotent");
        backend.fail = 5;
        Require(!mode.ChangeMonitor(L"headphones") && mode.State().monitorDevice == L"speakers", "A failed monitor switch must retain the current target");
        backend.fail = 0;
        Require(mode.ChangeMonitor(L"headphones").IsOk() && mode.State().monitorDevice == L"headphones", "Monitor selection must update after backend success");
        backend.failRestore = true;
        Require(!mode.Disable() && mode.State().cleanupPending, "Failed restore must remain visible and retryable");
        Require(backend.calls.back() == 'R', "A failed restore must preserve the active mirror and driver lease");
        backend.failRestore = false;
        Require(mode.Disable().IsOk() && !mode.State().enabled && !mode.State().cleanupPending, "Disable retry must complete cleanup");
        Require(backend.calls.ends_with("Rmv"), "Restore must precede mirror shutdown and virtual disconnection");
        Backend endpointFailure;
        vmex::recording::Mode endpointMode(endpointFailure);
        Require(endpointMode.Enable().IsOk(), "Endpoint shutdown test must start recording");
        endpointFailure.failDisconnect = true;
        Require(!endpointMode.Disable() && endpointMode.State().cleanupPending,
            "Failure to disable CABLE Input must remain pending instead of reporting recording off");
        Require(endpointFailure.calls.ends_with("Rmv"), "Restore defaults and stop monitoring before disabling CABLE Input");
        Require(!endpointMode.Enable(), "A pending endpoint disable must block a fresh recording session");
        endpointFailure.failDisconnect = false;
        Require(endpointMode.Disable().IsOk() && !endpointMode.State().cleanupPending && !endpointMode.State().enabled,
            "Endpoint disable retry must complete the transition to standby");
        Backend failedEnable;
        failedEnable.fail = 3;
        failedEnable.failDisconnect = true;
        vmex::recording::Mode failedEnableMode(failedEnable);
        Require(!failedEnableMode.Enable() && failedEnableMode.State().cleanupPending,
            "Mirror startup failure followed by endpoint disable failure must retain cleanup state");
        failedEnable.fail = 0;
        failedEnable.failDisconnect = false;
        Require(failedEnableMode.Disable().IsOk(), "Failed recording startup must be recoverable to standby");
        Backend partial;
        partial.fail = 4;
        partial.failRestore = true;
        vmex::recording::Mode pending(partial);
        Require(!pending.Enable() && pending.State().cleanupPending, "Partial default-role switch must preserve failed rollback state");
        Require(!pending.Enable(), "Recording cannot restart over pending cleanup");
        partial.failRestore = false;
        Require(pending.Disable().IsOk(), "Startup rollback must be retryable");
        Backend throwing;
        throwing.fail = 3;
        throwing.throwFailure = true;
        vmex::recording::Mode exceptional(throwing);
        Require(!exceptional.Enable() && throwing.calls == "SVMRmv", "Backend exceptions must restore defaults before stopping the mirror and disabling the virtual endpoint");
        Backend lost;
        vmex::recording::Mode live(lost);
        Require(live.Enable().IsOk(), "Health test startup failed");
        lost.mirrorFailed = true;
        Require(!live.CheckHealth() && !live.State().enabled, "Mirror failure must restore defaults and disable recording");
        Require(lost.calls.ends_with("Rmv"), "Device loss must release the driver lease after restoring defaults");
        Require(!live.CheckHealth(), "Runtime failure must remain visible until acknowledged");
        Require(live.Disable().IsOk() && live.CheckHealth().IsOk(), "Explicit disable must acknowledge runtime failure");
        vmex::recording::AudioFrameQueue queue(4, sizeof(int));
        const int first[] = { 1, 2, 3 };
        int output[5]{};
        Require(queue.Push(first, 3) == 0 && queue.Pop(output, 2) == 2 && output[0] == 1 && output[1] == 2,
                "Frame queue must preserve order");
        const int next[] = { 4, 5, 6, 7 };
        Require(queue.Push(next, 4) == 1 && queue.Size() == 4, "Overflow must discard the oldest whole frames");
        Require(queue.Pop(output, 5) == 4 && output[0] == 4 && output[3] == 7 && output[4] == 0,
                "Wrapped audio must remain ordered and underflow must produce silence");
        queue.Push(nullptr, 2);
        Require(queue.Pop(output, 2) == 2 && output[0] == 0 && output[1] == 0, "Silent capture packets must enqueue silence");
        vmex::recording::Snapshot state;
        Require(!vmex::recording::Request(L"unknown", L"", state), "Unknown recording action must be rejected before IPC");
        Require(!vmex::recording::Request(L"monitor", L"bad\nendpoint", state), "Recording device must not inject pipe commands");
    }

    void RoutingStore()
    {
        const auto com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Require(SUCCEEDED(com), "Routing integration requires COM");
        struct Apartment final { ~Apartment() { ::CoUninitialize(); } } apartment;
        auto enumerator = vmex::audio::detail::MakeEnumerator();
        vmex::audio::detail::Ptr<IMMDevice> output;
        Require(enumerator && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, output.put())),
            "Routing integration requires a default output");
        struct Stream final
        {
            vmex::audio::detail::Ptr<IAudioClient> client;
            ~Stream() { if (client) client->Stop(); }
        } stream;
        Require(SUCCEEDED(output->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, stream.client.putv())), "Test audio client unavailable");
        WAVEFORMATEX* format = nullptr;
        Require(SUCCEEDED(stream.client->GetMixFormat(&format)), "Test audio format unavailable");
        const auto initialized = stream.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, 1000000, 0, format, nullptr);
        ::CoTaskMemFree(format);
        Require(SUCCEEDED(initialized) && SUCCEEDED(stream.client->Start()), "Silent test stream must register its audio session");
        struct Restore final
        {
            std::wstring previous;
            bool saved = false;
            ~Restore()
            {
                if (saved) vmex::audio::detail::SetPersistedEndpointForRole(eRender, ::GetCurrentProcessId(), eConsole, previous);
            }
        } restore;
        const auto policy = vmex::audio::detail::MakePolicyFactory();
        Require(static_cast<bool>(policy), "Audio policy factory must be available");
        HSTRING previous = nullptr;
        const auto queried = policy->GetPersistedDefaultAudioEndpoint(::GetCurrentProcessId(), eRender, eConsole, &previous);
        if (previous)
        {
            UINT32 length = 0;
            const auto* value = ::WindowsGetStringRawBuffer(previous, &length);
            restore.previous = vmex::audio::detail::UnpackDeviceId(std::wstring(value, length));
            ::WindowsDeleteString(previous);
        }
        if (FAILED(queried)) std::wcerr << L"Initial policy query: " << std::hex << queried << L'\n';
        Require(SUCCEEDED(queried) || queried == HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
            "Cannot safely save test process routing");
        std::wstring endpoint;
        Require(vmex::audio::detail::GetDefaultEndpointId(eRender, endpoint), "Routing integration requires an output endpoint");
        if (endpoint.starts_with(L"{3."))
        {
            std::cout << "routing-live skipped: remote-session output cannot be persisted as an application route\n";
            return;
        }
        restore.saved = true;
        const auto packed = vmex::audio::detail::PackDeviceId(endpoint, eRender);
        HSTRING target = nullptr;
        Require(SUCCEEDED(::WindowsCreateString(packed.c_str(), static_cast<UINT32>(packed.size()), &target)), "Cannot pack test endpoint");
        const auto routed = policy->SetPersistedDefaultAudioEndpoint(::GetCurrentProcessId(), eRender, eConsole, target);
        ::WindowsDeleteString(target);
        if (FAILED(routed)) std::wcerr << L"Routing endpoint " << endpoint << L" result " << std::hex << routed << L'\n';
        Require(SUCCEEDED(routed), "Cannot set isolated test process routing");
        HKEY root = nullptr;
        Require(::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Multimedia\\Audio\\DefaultEndpoint", 0, KEY_READ, &root) == ERROR_SUCCESS,
            "Windows routing preference store must exist");
        struct Key final { HKEY value; ~Key() { ::RegCloseKey(value); } } rootKey{ root };
        bool found = false;
        for (DWORD index = 0;; ++index)
        {
            wchar_t name[256]{};
            DWORD length = 256;
            const auto error = ::RegEnumKeyExW(root, index, name, &length, nullptr, nullptr, nullptr, nullptr);
            if (error == ERROR_NO_MORE_ITEMS) break;
            Require(error == ERROR_SUCCESS, "Routing preference keys must be readable");
            HKEY child = nullptr;
            if (::RegOpenKeyExW(root, name, 0, KEY_READ, &child) != ERROR_SUCCESS) continue;
            Key childKey{ child };
            wchar_t identity[32768]{};
            DWORD bytes = sizeof(identity), type = 0;
            if (::RegQueryValueExW(child, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(identity), &bytes) != ERROR_SUCCESS || type != REG_SZ) continue;
            const auto path = vmex::strings::ToLower(identity);
            if (path.find(L"vmex_tests.exe") == std::wstring::npos) continue;
            wchar_t value[4096]{};
            bytes = sizeof(value);
            Require(::RegQueryValueExW(child, L"000_000", nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS && type == REG_SZ,
                "Output routing must use the expected string preference schema");
            Require(vmex::audio::detail::UnpackDeviceId(value) == endpoint,
                "Stored endpoint must match the policy API for the isolated test process");
            found = true;
        }
        Require(found, "The isolated test process routing entry must be discoverable");
    }

    void DriverTrust()
    {
        const auto source = vmex::platform::GetExecutableDirectory() / L"Driver" / L"VBCABLE";
        bool testSigned = false;
        const auto verified = vmex::recording::ValidateDriverPackage(source, testSigned);
        if (!verified) std::wcerr << verified.detail << L'\n';
        Require(static_cast<bool>(verified), "Signed driver package must verify without importing certificates");
        struct Scratch final
        {
            std::filesystem::path path = std::filesystem::temp_directory_path() /
                (L"Vmex.DriverTrust." + std::to_wstring(::GetCurrentProcessId()) + L"." + std::to_wstring(::GetTickCount64()));
            Scratch() { std::filesystem::create_directory(path); }
            ~Scratch() { std::error_code error; std::filesystem::remove_all(path, error); }
        } scratch;
        Require(!testSigned, "VB-CABLE must not use test signing");
        std::filesystem::copy(source, scratch.path, std::filesystem::copy_options::recursive);
        for (const auto name : { L"vbaudio_cable64arm_win10.sys", L"VBCABLE_ControlPanel.exe", L"pin_in.ico", L"pin_out.ico", L"readme.txt" })
        {
            std::filesystem::remove(scratch.path / name);
            Require(!vmex::recording::ValidateDriverPackage(scratch.path, testSigned), "Every installer dependency must be present");
            std::filesystem::copy_file(source / name, scratch.path / name);
        }
        Require(vmex::recording::ValidateDriverPackage(scratch.path, testSigned).IsOk(), "Restored complete package must verify");
        const auto inf = scratch.path / L"vbMmeCable64_win10.inf";
        { std::ofstream changed(inf, std::ios::binary | std::ios::app); changed << "\nTampered=1\n"; }
        Require(!vmex::recording::ValidateDriverPackage(scratch.path, testSigned), "A modified INF must not verify against the catalog");
        std::filesystem::copy_file(source / L"vbMmeCable64_win10.inf", inf, std::filesystem::copy_options::overwrite_existing);
        const auto sys = scratch.path / L"vbaudio_cable64_win10.sys";
        { std::fstream changed(sys, std::ios::binary | std::ios::in | std::ios::out); changed.seekg(1024); const auto byte = changed.get(); changed.seekp(1024); changed.put(static_cast<char>(byte ^ 0x5A)); }
        Require(!vmex::recording::ValidateDriverPackage(scratch.path, testSigned), "A modified driver image must fail verification");
        std::filesystem::copy_file(source / L"vbaudio_cable64_win10.sys", sys, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(scratch.path / L"VBCABLE_Setup_x64.exe");
        Require(!vmex::recording::ValidateDriverPackage(scratch.path, testSigned), "A missing original installer must fail verification");
    }

    void Commands()
    {
        using vmex::inject::ParseTapCommand;
        const auto open = ParseTapCommand("MANAGER\r\n");
        Require(open && open->argumentsValid && open->verb == vmex::inject::TapVerb::Manager, "Manager command rejected");
        const auto invalid = ParseTapCommand("MANAGER untrusted.exe");
        Require(invalid && !invalid->argumentsValid, "Manager command must not accept an executable argument");
        const auto exit = ParseTapCommand("EXIT 1234");
        Require(exit && !exit->argumentsValid, "Exit must not accept an arbitrary target process");
        Require(!ParseTapCommand("unknown"), "Unknown commands must be rejected");
        Require(!ParseTapCommand("  \r\n"), "Empty command must be rejected");
        Require(ParseTapCommand("SETDEFAULT render endpoint")->argumentsValid, "Default command changed");
        Require(!ParseTapCommand("SETDEFAULT render")->argumentsValid, "Incomplete command accepted");
        Require(ParseTapCommand("CLEARREDIRECT")->argumentsValid, "Audio reset command rejected");
        Require(ParseTapCommand("RECORDING status")->argumentsValid, "Recording status command rejected");
        Require(!ParseTapCommand("RECORDING")->argumentsValid, "Recording requires an action");
    }

    void Resources()
    {
        vmex::text::TextService text;
        Require(text.Attach(vmex::text::CreateEmbeddedTextSource()).IsOk(), "Text resources unavailable");
        for (const auto key : { L"manager.title", L"manager.stop.confirm", L"manager.uninstall.confirm", L"manager.missing",
                               L"cmd.start.summary", L"cmd.stop.summary", L"cmd.uninstall.summary", L"cmd.uninstall.yes",
                                 L"cmd.show-driver-name.summary", L"cmd.panel.summary", L"manager.panel", L"manager.panel.pending",
                                 L"log.management.result", L"page.footer.settings" })
            Require(text.Contains(key) && !text.Resolve(key).empty(), "Required management text is missing");
        const auto message = text.ResolveFormat(L"manager.failed", { L"probe-error" });
        Require(message.find(L"probe-error") != std::wstring::npos, "Error details lost in localization");
        Require(text.MissingKeys().empty(), "Resource lookup failed");
    }

    void Config()
    {
        const auto root = std::filesystem::temp_directory_path() /
            (L"vmex-tests-" + std::to_wstring(::GetCurrentProcessId()));
        std::filesystem::create_directories(root);
        const auto file = root / L"中文 配置.ini";
        vmex::config::ConfigService source;
        Require(source.Load(file).IsOk(), "Missing configuration must load defaults");
        Require(!std::filesystem::exists(file), "Reading defaults must not write configuration");
        source.SetBool(L"general", L"visible", false);
        source.SetString(L"general", L"name", L"声音 面板");
        Require(source.Save().IsOk(), "Configuration save failed");
        vmex::config::ConfigService restored;
        Require(restored.Load(file).IsOk(), "Configuration reload failed");
        Require(!restored.GetBool(L"general", L"visible", true), "Boolean configuration did not persist");
        Require(restored.GetString(L"general", L"name", L"") == L"声音 面板", "Unicode configuration did not round trip");
        Require(restored.SourceEncodingIsCanonical(), "Configuration encoding is not canonical");
        std::filesystem::remove(file);
        Require(!std::filesystem::exists(file), "Configuration cleanup failed");
        std::filesystem::remove(root);
    }

    void Pipe()
    {
        struct Handler final : vmex::inject::ITapCommandHandler
        {
            std::atomic<int> managers = 0;
            void HandleTapCommand(const vmex::inject::TapCommand& command) override
            {
                if (command.verb == vmex::inject::TapVerb::Manager) ++managers;
            }
            std::string HandleTapRequest(const vmex::inject::TapCommand& command) override
            {
                if (command.verb == vmex::inject::TapVerb::Recording) return "0\t1\t0\tmonitor\t\n";
                HandleTapCommand(command);
                return {};
            }
        } handler;
        const auto name = L"\\\\.\\pipe\\VmExt.Test." + std::to_wstring(::GetCurrentProcessId());
        vmex::inject::TapPipeServer server(name, handler);
        Require(server.Start(), "Pipe server failed to start");
        HANDLE pipe = INVALID_HANDLE_VALUE;
        for (int attempt = 0; attempt < 100 && pipe == INVALID_HANDLE_VALUE; ++attempt)
        {
            pipe = ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) ::Sleep(10);
        }
        Require(pipe != INVALID_HANDLE_VALUE, "Pipe connection failed");
        const auto write = [pipe](const std::string& text) {
            DWORD written = 0;
            Require(::WriteFile(pipe, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                    written == text.size(), "Pipe write failed");
        };
        write("MANA");
        ::Sleep(20);
        write("GER\nMANAGER arbitrary.exe\nunknown\n");
        for (int attempt = 0; attempt < 100 && handler.managers.load() != 1; ++attempt) ::Sleep(10);
        Require(handler.managers.load() == 1, "Fragmented command or argument validation failed");
        write("RECORDING status\n");
        DWORD available = 0;
        for (int attempt = 0; attempt < 100 && !available; ++attempt)
        {
            Require(::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) != FALSE, "Recording reply pipe closed early");
            if (!available) ::Sleep(10);
        }
        Require(available > 0, "Recording request did not receive a reply");
        char response[128]{};
        DWORD read = 0;
        Require(::ReadFile(pipe, response, sizeof(response), &read, nullptr) != FALSE &&
            std::string(response, read) == "0\t1\t0\tmonitor\t\n", "Recording reply was truncated or altered");
        std::string unread;
        for (int i = 0; i < 300; ++i) unread += "RECORDING status\n";
        write(unread);
        ::Sleep(100);
        const auto start = std::chrono::steady_clock::now();
        server.Stop();
        const auto elapsed = std::chrono::steady_clock::now() - start;
        ::CloseHandle(pipe);
        Require(elapsed < std::chrono::seconds(3), "An idle connected client must not prevent server shutdown");
        Require(!server.IsRunning(), "Pipe server did not stop");
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "guard-parent") { GuardParent(argv[2]); return 0; }
        Require(argc == 2, "A test suite name is required");
        const std::string suite(argv[1]);
        if (suite == "uninstall") UninstallFailures();
        else if (suite == "audio") AudioFailures();
        else if (suite == "audio-live") AudioLiveReset();
        else if (suite == "routing-live") RoutingStore();
        else if (suite == "recording") Recording();
        else if (suite == "guard") GuardRecovery();
        else if (suite == "endpoint-lock") EndpointLockPolicy();
        else if (suite == "presentation") Presentation();
        else if (suite == "driver-trust") DriverTrust();
        else if (suite == "commands") Commands();
        else if (suite == "resources") Resources();
        else if (suite == "config") Config();
        else if (suite == "pipe") Pipe();
        else throw std::runtime_error("Unknown test suite");
        std::cout << suite << " passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
