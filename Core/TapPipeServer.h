#pragma once

#include "Foundation.h"
#include "TapCommand.h"

#include <atomic>
#include <thread>

namespace vmex::inject
{
    class ITapCommandHandler
    {
    public:
        virtual ~ITapCommandHandler() = default;

        virtual void HandleTapCommand(const TapCommand& command) = 0;
    };

    class TapPipeServer final
    {
    public:
        TapPipeServer(std::wstring pipeName, ITapCommandHandler& handler);
        ~TapPipeServer();

        TapPipeServer(const TapPipeServer&) = delete;
        TapPipeServer& operator=(const TapPipeServer&) = delete;
        TapPipeServer(TapPipeServer&&) = delete;
        TapPipeServer& operator=(TapPipeServer&&) = delete;

        bool Start();
        void Stop();

        [[nodiscard]] bool IsRunning() const noexcept;
        [[nodiscard]] const std::wstring& PipeName() const noexcept;

    private:
        void Loop();
        void Dispatch(std::string_view line);

        std::wstring m_pipeName;
        ITapCommandHandler& m_handler;
        std::atomic<bool> m_running{ false };
        std::thread m_thread;
    };
}
