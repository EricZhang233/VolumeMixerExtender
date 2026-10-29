#pragma once

#include "Foundation.h"
#include <windows.h>
#include <array>

namespace vmex::recording
{
    class IRecoveryActions
    {
    public:
        virtual ~IRecoveryActions() = default;
        virtual Status RestoreDefaults() = 0;
        virtual Status ClearRedirects() = 0;
        virtual Status DisableEndpoint() = 0;
    };
    Status RecoverAfterHostExit(IRecoveryActions& actions);

    struct Guard final
    {
        HANDLE process = nullptr;
        HANDLE stop = nullptr;
        HANDLE targets = nullptr;
        ~Guard();
        Status Start(const std::array<std::wstring, 2>& defaults, std::wstring_view endpoint);
        Status UpdateDefaults(const std::array<std::wstring, 2>& defaults);
        void Stop() noexcept;
        void Abandon() noexcept;
        [[nodiscard]] bool Running() const;
    };
    int RunGuard(const std::vector<std::wstring>& arguments);
}
