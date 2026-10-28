#include "RecordingClient.h"

#include "HostPresence.h"
#include "ManagementService.h"
#include "Platform.h"
#include "Strings.h"
#include <windows.h>

namespace vmex::recording
{
    Status Request(std::wstring_view action, std::wstring_view device, Snapshot& state)
    {
        state = {};
        if (action != L"on" && action != L"off" && action != L"status" && action != L"monitor")
            return Status::InvalidArguments(L"recording-action");
        if ((action == L"monitor") != !device.empty() || device.find_first_of(L" \t\r\n") != std::wstring_view::npos)
            return Status::InvalidArguments(L"recording-device");
        if (action == L"on")
        {
            const auto started = management::StartHost();
            if (!started) return started;
        }
        if (!host::HostPresence::IsRunning())
        {
            state.known = true;
            return action == L"status" || action == L"off" ? Status::Ok() : Status::Failed(L"recording-host-stopped");
        }
        auto host = management::ConnectHost();
        if (!host.Valid()) return Status::Failed(L"recording-connect");
        const HANDLE pipe = static_cast<HANDLE>(host.pipe);
        const auto line = strings::ToNarrow(L"RECORDING " + std::wstring(action) +
            (device.empty() ? L"" : L" " + std::wstring(device)) + L"\n");
        DWORD written = 0;
        if (!::WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) || written != line.size())
            return Status::FromLastError(L"recording-write");
        std::string response;
        const auto deadline = ::GetTickCount64() + 15000;
        while (::GetTickCount64() < deadline)
        {
            DWORD available = 0;
            if (!::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return Status::FromLastError(L"recording-response");
            if (!available) { ::Sleep(10); continue; }
            char bytes[1024];
            DWORD read = 0;
            if (!::ReadFile(pipe, bytes, sizeof(bytes), &read, nullptr) || !read) return Status::FromLastError(L"recording-read");
            response.append(bytes, read);
            if (response.size() > 8192) return Status::Failed(L"recording-response-size");
            const auto newline = response.find('\n');
            if (newline == std::string::npos) continue;
            const auto fields = strings::Split(strings::ToWide(response.substr(0, newline)), L'\t');
            std::int64_t code = 0;
            if (fields.size() != 5 || !strings::TryParseInt(fields[0], code) || code < 0 || code > 3 ||
                (fields[1] != L"0" && fields[1] != L"1") || (fields[2] != L"0" && fields[2] != L"1"))
                return Status::Failed(L"recording-protocol");
            state.enabled = fields[1] == L"1";
            state.known = true;
            state.cleanupPending = fields[2] == L"1";
            state.monitorDevice = fields[3];
            return { static_cast<Code>(code), fields[4] };
        }
        return Status::Failed(L"recording-response-timeout");
    }
}
