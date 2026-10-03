#include "TapPipeServer.h"

#include "Logger.h"
#include "Strings.h"

#include <windows.h>

#include <array>
#include <utility>

namespace vmex::inject
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"pipe";
        constexpr std::size_t kReadBufferSize = 512;
        constexpr std::size_t kMaxAccumulated = 4096;
    }

    TapPipeServer::TapPipeServer(std::wstring pipeName, ITapCommandHandler& handler)
        : m_pipeName(std::move(pipeName))
        , m_handler(handler)
    {
    }

    TapPipeServer::~TapPipeServer()
    {
        Stop();
    }

    bool TapPipeServer::IsRunning() const noexcept
    {
        return m_running.load();
    }

    const std::wstring& TapPipeServer::PipeName() const noexcept
    {
        return m_pipeName;
    }

    bool TapPipeServer::Start()
    {
        if (m_running.exchange(true))
        {
            return true;
        }

        m_thread = std::thread([this]() { Loop(); });
        return true;
    }

    void TapPipeServer::Stop()
    {
        const bool wasRunning = m_running.exchange(false);
        if (wasRunning)
        {
            const HANDLE wake = ::CreateFileW(m_pipeName.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (wake != INVALID_HANDLE_VALUE)
            {
                ::CloseHandle(wake);
            }
        }

        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    void TapPipeServer::Loop()
    {
        while (m_running.load())
        {
            const HANDLE pipe = ::CreateNamedPipeW(
                m_pipeName.c_str(),
                PIPE_ACCESS_INBOUND,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,
                4096,
                4096,
                0,
                nullptr);

            if (pipe == INVALID_HANDLE_VALUE)
            {
                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Warn,
                    kChannel,
                    L"log.pipe.create_failed",
                    { std::to_wstring(::GetLastError()) });
                ::Sleep(1000);
                continue;
            }

            bool connected = false;
            if (::ConnectNamedPipe(pipe, nullptr) != FALSE)
            {
                connected = true;
            }
            else
            {
                const DWORD error = ::GetLastError();
                if (error == ERROR_PIPE_CONNECTED || error == ERROR_NO_DATA)
                {
                    connected = true;
                }
                else
                {
                    log::Logger::Instance().WriteKeyFormat(
                        log::Level::Warn,
                        kChannel,
                        L"log.pipe.connect_failed",
                        { std::to_wstring(error) });
                }
            }

            if (connected)
            {
                std::string accumulated;
                std::array<char, kReadBufferSize> buffer{};
                for (;;)
                {
                    DWORD read = 0;
                    if (::ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) == FALSE || read == 0)
                    {
                        break;
                    }

                    accumulated.append(buffer.data(), read);

                    std::size_t newline = 0;
                    while ((newline = accumulated.find('\n')) != std::string::npos)
                    {
                        std::string line = accumulated.substr(0, newline);
                        accumulated.erase(0, newline + 1);
                        if (!line.empty() && line.back() == '\r')
                        {
                            line.pop_back();
                        }
                        if (!line.empty())
                        {
                            Dispatch(line);
                        }
                    }

                    if (accumulated.size() > kMaxAccumulated)
                    {
                        accumulated.clear();
                    }
                }

                ::FlushFileBuffers(pipe);
                ::DisconnectNamedPipe(pipe);
            }

            ::CloseHandle(pipe);
        }
    }

    void TapPipeServer::Dispatch(std::string_view line)
    {
        const auto command = ParseTapCommand(line);
        if (!command.has_value())
        {
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Warn,
                kChannel,
                L"log.pipe.unknown",
                { strings::ToWide(line) });
            return;
        }

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info,
            kChannel,
            L"log.pipe.received",
            { std::wstring(ToString(command->verb)), strings::ToWide(line) });

        if (!command->argumentsValid)
        {
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Warn,
                kChannel,
                L"log.pipe.invalid",
                { strings::ToWide(line) });
            return;
        }

        try
        {
            m_handler.HandleTapCommand(*command);
        }
        catch (...)
        {
            log::Logger::Instance().WriteKey(log::Level::Error, kChannel, L"log.pipe.handler_threw");
        }
    }
}
