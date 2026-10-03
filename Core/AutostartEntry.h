#pragma once

#include <string>

namespace vmex::autostart
{
    class AutostartEntry final
    {
    public:
        static constexpr wchar_t kValueName[] = L"VolumeMixerExtender";

        [[nodiscard]] static bool IsEnabled();
        static bool Enable(std::wstring& error);
        static bool Disable(std::wstring& error);
    };
}
