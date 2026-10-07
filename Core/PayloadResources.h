#pragma once

#include "Foundation.h"
#include "PayloadResources.rc.h"

namespace vmex::payload
{
    enum class Item
    {
        Launcher = 0,
        Tap = 1,
        Diagnostics = 2,
        NotificationToolkit = 3,
        Core = 4
    };

    [[nodiscard]] std::wstring_view ToString(Item item);
    [[nodiscard]] unsigned ResourceId(Item item);
    [[nodiscard]] std::filesystem::path DefaultFileName(Item item);

    [[nodiscard]] bool Exists(Item item);
    [[nodiscard]] std::uint32_t Size(Item item);
    [[nodiscard]] std::uint64_t PayloadContentHash();
    [[nodiscard]] Status Extract(Item item, const std::filesystem::path& destination);
    [[nodiscard]] Status ExtractNotificationToolkit(const std::filesystem::path& destination);

    struct ExtractedPair final
    {
        std::filesystem::path launcher;
        std::filesystem::path tap;
        std::filesystem::path diagnostics;
    };

    [[nodiscard]] Status ExtractPair(const std::filesystem::path& directory, ExtractedPair& pair);
}
