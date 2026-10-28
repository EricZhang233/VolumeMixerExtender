#include "MaintenanceWorkflow.h"
#include "ConfigService.h"
#include "TapCommand.h"
#include "TapPipeServer.h"
#include "TextService.h"
#include "Platform.h"
#include "AudioInterop.h"
#include "RecordingMode.h"
#include "AudioFrameQueue.h"
#include "RecordingClient.h"
#include "RecordingDriverService.h"

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

    void Recording()
    {
        struct Backend final : vmex::recording::IBackend
        {
            std::string calls;
            int fail = 0;
            bool failRestore = false;
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
            void DisconnectVirtualDevice() noexcept override { calls += 'v'; }
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
            const std::string expected[] = { "", "S", "SVv", "SVMmv", "SVMDRmv" };
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
        Require(!exceptional.Enable() && throwing.calls == "SVMmv", "Backend exceptions must release mirror and virtual endpoint");
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

    void DriverTrust()
    {
        const auto source = vmex::platform::GetExecutableDirectory() / L"Driver";
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
        for (const auto name : { L"vmex_recording.sys", L"vmex_recording.inf", L"vmex_recording.cat", L"vmex_recording.cer" })
            std::filesystem::copy_file(source / name, scratch.path / name);
        const auto inf = scratch.path / L"vmex_recording.inf";
        { std::ofstream changed(inf, std::ios::binary | std::ios::app); changed << "\nTampered=1\n"; }
        Require(!vmex::recording::ValidateDriverPackage(scratch.path, testSigned), "A modified INF must not verify against the catalog");
        std::filesystem::copy_file(source / L"vmex_recording.inf", inf, std::filesystem::copy_options::overwrite_existing);
        const auto sys = scratch.path / L"vmex_recording.sys";
        { std::fstream changed(sys, std::ios::binary | std::ios::in | std::ios::out); changed.seekg(1024); const auto byte = changed.get(); changed.seekp(1024); changed.put(static_cast<char>(byte ^ 0x5A)); }
        Require(!vmex::recording::ValidateDriverPackage(scratch.path, testSigned), "A modified driver image must fail verification");
        std::filesystem::copy_file(source / L"vmex_recording.sys", sys, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(scratch.path / L"vmex_recording.cer");
        Require(!vmex::recording::ValidateDriverPackage(scratch.path, testSigned), "A missing signer certificate must fail verification");
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
        Require(argc == 2, "A test suite name is required");
        const std::string suite(argv[1]);
        if (suite == "uninstall") UninstallFailures();
        else if (suite == "audio") AudioFailures();
        else if (suite == "audio-live") AudioLiveReset();
        else if (suite == "recording") Recording();
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
