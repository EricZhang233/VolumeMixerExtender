#pragma once

#include "Foundation.h"

namespace vmex::payload
{
    enum class Item
    {
        Launcher = 0,
        Tap = 1,
        Diagnostics = 2,
        Core = 3
    };

    [[nodiscard]] std::wstring_view ToString(Item item);
    [[nodiscard]] std::filesystem::path DefaultFileName(Item item);

    [[nodiscard]] bool Exists(Item item);
    [[nodiscard]] std::uint32_t Size(Item item);
    [[nodiscard]] Status Extract(Item item, const std::filesystem::path& destination);

    struct ExtractedPair final
    {
        std::filesystem::path launcher;
        std::filesystem::path tap;
        std::filesystem::path diagnostics;
    };

    [[nodiscard]] Status ExtractPair(const std::filesystem::path& directory, ExtractedPair& pair);
}
