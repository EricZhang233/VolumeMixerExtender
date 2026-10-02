#pragma once

#include "Foundation.h"

#include <span>

namespace vmex::encoding
{
    enum class Encoding
    {
        Unknown = 0,
        Utf8 = 1,
        Utf8Bom = 2,
        Utf16Le = 3,
        Utf16LeBom = 4,
        Utf16Be = 5,
        Utf16BeBom = 6,
        Ansi = 7
    };

    [[nodiscard]] std::wstring_view ToString(Encoding encoding);
    [[nodiscard]] bool HasBom(Encoding encoding);
    [[nodiscard]] bool IsUnicode(Encoding encoding);

    struct Decoded final
    {
        Encoding encoding = Encoding::Unknown;
        std::wstring text;
        std::uint32_t codePage = 0;
    };

    [[nodiscard]] Encoding Detect(std::span<const std::byte> bytes);
    [[nodiscard]] Status Decode(std::span<const std::byte> bytes, Decoded& decoded);
    [[nodiscard]] Status DecodeWithEncoding(std::span<const std::byte> bytes, Encoding encoding, Decoded& decoded);
    [[nodiscard]] std::vector<std::byte> EncodeUtf16Le(std::wstring_view text, bool withBom);
    [[nodiscard]] std::vector<std::byte> EncodeUtf8(std::wstring_view text, bool withBom);
}
