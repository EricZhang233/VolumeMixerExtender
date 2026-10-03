#pragma once

#include "Foundation.h"

namespace vmex::icons
{
    [[nodiscard]] Status FileForProcess(std::uint32_t processId, const std::filesystem::path& cacheRoot, std::wstring& pngFile);

    [[nodiscard]] Status FileForImage(const std::filesystem::path& image, const std::filesystem::path& cacheRoot, std::wstring& pngFile);
}
