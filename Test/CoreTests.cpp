#include "MaintenanceWorkflow.h"
#include "ConfigService.h"
#include "TapCommand.h"
#include "TapPipeServer.h"
#include "TextService.h"
#include "Platform.h"

#include <windows.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <chrono>

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

    void AudioFailures()
    {
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
        } handler;
        const auto name = L"\\\\.\\pipe\\VmExt.Test." + std::to_wstring(::GetCurrentProcessId());
        vmex::inject::TapPipeServer server(name, handler);
        Require(server.Start(), "Pipe server failed to start");
        HANDLE pipe = INVALID_HANDLE_VALUE;
        for (int attempt = 0; attempt < 100 && pipe == INVALID_HANDLE_VALUE; ++attempt)
        {
            pipe = ::CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
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
