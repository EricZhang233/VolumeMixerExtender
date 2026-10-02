#include "Strings.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace vmex::strings
{
    std::wstring ToWide(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const auto length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0)
        {
            return {};
        }

        std::wstring result(static_cast<std::size_t>(length), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
        return result;
    }

    std::string ToNarrow(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const auto length = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0)
        {
            return {};
        }

        std::string result(static_cast<std::size_t>(length), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
        return result;
    }

    std::wstring_view TrimView(std::wstring_view text)
    {
        std::size_t begin = 0;
        std::size_t end = text.size();
        while (begin < end && std::iswspace(text[begin]) != 0)
        {
            ++begin;
        }
        while (end > begin && std::iswspace(text[end - 1]) != 0)
        {
            --end;
        }
        return text.substr(begin, end - begin);
    }

    std::wstring Trim(std::wstring_view text)
    {
        return std::wstring(TrimView(text));
    }

    std::wstring ToLower(std::wstring_view text)
    {
        std::wstring result(text);
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        return result;
    }

    std::wstring ToUpper(std::wstring_view text)
    {
        std::wstring result(text);
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
        return result;
    }

    std::vector<std::wstring> Split(std::wstring_view text, wchar_t separator)
    {
        std::vector<std::wstring> parts;
        std::size_t start = 0;
        for (;;)
        {
            const auto position = text.find(separator, start);
            if (position == std::wstring_view::npos)
            {
                parts.emplace_back(text.substr(start));
                break;
            }
            parts.emplace_back(text.substr(start, position - start));
            start = position + 1;
        }
        return parts;
    }

    std::wstring Join(const std::vector<std::wstring>& parts, std::wstring_view separator)
    {
        std::wstring result;
        for (std::size_t index = 0; index < parts.size(); ++index)
        {
            if (index != 0)
            {
                result.append(separator);
            }
            result.append(parts[index]);
        }
        return result;
    }

    std::wstring Replace(std::wstring_view text, std::wstring_view from, std::wstring_view to)
    {
        if (from.empty())
        {
            return std::wstring(text);
        }

        std::wstring result;
        std::size_t start = 0;
        for (;;)
        {
            const auto position = text.find(from, start);
            if (position == std::wstring_view::npos)
            {
                result.append(text.substr(start));
                break;
            }
            result.append(text.substr(start, position - start));
            result.append(to);
            start = position + from.size();
        }
        return result;
    }

    bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right)
    {
        if (left.size() != right.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (std::towlower(left[index]) != std::towlower(right[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool StartsWith(std::wstring_view text, std::wstring_view prefix)
    {
        return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

    bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix)
    {
        return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
    }

    std::wstring Format(std::wstring_view pattern, const std::vector<std::wstring>& arguments)
    {
        std::wstring result;
        for (std::size_t index = 0; index < pattern.size(); ++index)
        {
            const auto ch = pattern[index];
            if (ch != L'{')
            {
                result.push_back(ch);
                continue;
            }

            const auto close = pattern.find(L'}', index);
            if (close == std::wstring_view::npos)
            {
                result.push_back(ch);
                continue;
            }

            const auto token = pattern.substr(index + 1, close - index - 1);
            std::size_t position = 0;
            bool numeric = !token.empty();
            for (const auto digit : token)
            {
                if (digit < L'0' || digit > L'9')
                {
                    numeric = false;
                    break;
                }
                position = (position * 10) + static_cast<std::size_t>(digit - L'0');
            }

            if (!numeric)
            {
                result.push_back(ch);
                continue;
            }

            if (position < arguments.size())
            {
                result.append(arguments[position]);
            }
            index = close;
        }
        return result;
    }

    std::wstring FromDouble(double value, int precision)
    {
        wchar_t buffer[64] = {};
        swprintf_s(buffer, L"%.*f", precision, value);
        return std::wstring(buffer);
    }

    bool TryParseDouble(std::wstring_view text, double& value)
    {
        const std::wstring owned(TrimView(text));
        if (owned.empty())
        {
            return false;
        }

        wchar_t* end = nullptr;
        const auto parsed = std::wcstod(owned.c_str(), &end);
        if (end == owned.c_str() || *end != L'\0')
        {
            return false;
        }
        value = parsed;
        return true;
    }

    bool TryParseInt(std::wstring_view text, std::int64_t& value)
    {
        const std::wstring owned(TrimView(text));
        if (owned.empty())
        {
            return false;
        }

        wchar_t* end = nullptr;
        const auto parsed = std::wcstoll(owned.c_str(), &end, 10);
        if (end == owned.c_str() || *end != L'\0')
        {
            return false;
        }
        value = parsed;
        return true;
    }

    bool TryParseBool(std::wstring_view text, bool& value)
    {
        const auto lowered = ToLower(TrimView(text));
        if (lowered == L"1" || lowered == L"true" || lowered == L"yes" || lowered == L"on")
        {
            value = true;
            return true;
        }
        if (lowered == L"0" || lowered == L"false" || lowered == L"no" || lowered == L"off")
        {
            value = false;
            return true;
        }
        return false;
    }

    std::wstring ToHex(std::uint64_t value, int width)
    {
        wchar_t buffer[32] = {};
        swprintf_s(buffer, L"%0*llX", width, static_cast<unsigned long long>(value));
        return std::wstring(buffer);
    }
}
