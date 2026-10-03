#pragma once

#include "Foundation.h"

#include <span>

namespace vmex::platform
{
    inline constexpr std::uint32_t kMinimumWindowsBuild = VMEX_MIN_WINDOWS_BUILD;
    inline constexpr std::wstring_view kMinimumWindowsName = L"26H2";

    struct WindowsVersion final
    {
        std::uint32_t major = 0;
        std::uint32_t minor = 0;
        std::uint32_t build = 0;
        std::uint32_t revision = 0;
    };
    [[nodiscard]] WindowsVersion GetWindowsVersion();
    [[nodiscard]] bool IsSupportedWindowsVersion(const WindowsVersion& version);
    [[nodiscard]] Status VerifySupportedWindowsVersion();
    [[nodiscard]] std::wstring DescribeWindowsVersion(const WindowsVersion& version);
    [[nodiscard]] std::filesystem::path GetExecutablePath();
    [[nodiscard]] std::filesystem::path GetExecutableDirectory();
    [[nodiscard]] std::filesystem::path GetLocalAppDataDirectory();

    inline constexpr std::wstring_view kInstallDirectoryName = L"VolumeMixerExtender";
    [[nodiscard]] std::filesystem::path GetInstallDirectory();
    [[nodiscard]] std::filesystem::path GetCacheDirectory();
    [[nodiscard]] std::wstring GetMachineName();
    [[nodiscard]] std::uint32_t GetCurrentSessionId();
    [[nodiscard]] bool FileExists(const std::filesystem::path& file);
    [[nodiscard]] Status ReadAllBytes(const std::filesystem::path& file, std::vector<std::byte>& bytes);
    [[nodiscard]] Status WriteAllBytes(const std::filesystem::path& file, std::span<const std::byte> bytes);
}
