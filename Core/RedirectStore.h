#pragma once

#include "Foundation.h"

namespace vmex::audio
{
    inline constexpr std::wstring_view kRedirectStoreFileName = L"redirects.ini";
    inline constexpr std::wstring_view kRedirectStoreSection = L"apps";

    [[nodiscard]] std::filesystem::path RedirectStorePath(const std::filesystem::path& cacheRoot);

    [[nodiscard]] Status SetAppRedirect(const std::filesystem::path& cacheRoot, std::wstring_view appKey, std::wstring_view deviceId);

    [[nodiscard]] std::wstring GetAppRedirect(const std::filesystem::path& cacheRoot, std::wstring_view appKey);
}
