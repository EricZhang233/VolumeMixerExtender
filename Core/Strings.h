#pragma once

#include "Foundation.h"

namespace vmex::strings
{
    [[nodiscard]] std::wstring ToWide(std::string_view text);
    [[nodiscard]] std::string ToNarrow(std::wstring_view text);
    [[nodiscard]] std::wstring_view TrimView(std::wstring_view text);
    [[nodiscard]] std::wstring Trim(std::wstring_view text);
    [[nodiscard]] std::wstring ToLower(std::wstring_view text);
    [[nodiscard]] std::wstring ToUpper(std::wstring_view text);
    [[nodiscard]] std::vector<std::wstring> Split(std::wstring_view text, wchar_t separator);
    [[nodiscard]] std::wstring Join(const std::vector<std::wstring>& parts, std::wstring_view separator);
    [[nodiscard]] std::wstring Replace(std::wstring_view text, std::wstring_view from, std::wstring_view to);
    [[nodiscard]] bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right);
    [[nodiscard]] bool StartsWith(std::wstring_view text, std::wstring_view prefix);
    [[nodiscard]] bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix);
    [[nodiscard]] std::wstring Format(std::wstring_view pattern, const std::vector<std::wstring>& arguments);
    [[nodiscard]] std::wstring FromDouble(double value, int precision);
    [[nodiscard]] bool TryParseDouble(std::wstring_view text, double& value);
    [[nodiscard]] bool TryParseInt(std::wstring_view text, std::int64_t& value);
    [[nodiscard]] bool TryParseBool(std::wstring_view text, bool& value);
    [[nodiscard]] std::wstring ToHex(std::uint64_t value, int width);
}
