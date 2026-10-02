#include "TextEncoding.h"

#include "Strings.h"

#include <windows.h>

namespace vmex::encoding
{
    namespace
    {
        constexpr std::byte kByte(std::uint8_t value)
        {
            return static_cast<std::byte>(value);
        }

        bool StartsWith(std::span<const std::byte> bytes, std::initializer_list<std::uint8_t> prefix)
        {
            if (bytes.size() < prefix.size())
            {
                return false;
            }

            std::size_t index = 0;
            for (const auto value : prefix)
            {
                if (bytes[index] != kByte(value))
                {
                    return false;
                }
                ++index;
            }
            return true;
        }

        std::size_t BomLength(Encoding encoding)
        {
            switch (encoding)
            {
            case Encoding::Utf8Bom:
                return 3;
            case Encoding::Utf16LeBom:
            case Encoding::Utf16BeBom:
                return 2;
            default:
                return 0;
            }
        }

        bool LooksLikeUtf16(std::span<const std::byte> bytes, bool& littleEndian)
        {
            if (bytes.size() < 4)
            {
                return false;
            }

            const auto sample = std::min<std::size_t>(bytes.size(), 512);
            std::size_t oddZeros = 0;
            std::size_t evenZeros = 0;
            for (std::size_t index = 0; index + 1 < sample; index += 2)
            {
                if (bytes[index] == kByte(0x00))
                {
                    ++evenZeros;
                }
                if (bytes[index + 1] == kByte(0x00))
                {
                    ++oddZeros;
                }
            }

            const auto pairs = sample / 2;
            if (oddZeros * 2 > pairs && evenZeros == 0)
            {
                littleEndian = true;
                return true;
            }
            if (evenZeros * 2 > pairs && oddZeros == 0)
            {
                littleEndian = false;
                return true;
            }
            return false;
        }

        bool LooksLikeUtf8(std::span<const std::byte> bytes)
        {
            std::size_t index = 0;
            while (index < bytes.size())
            {
                const auto value = std::to_integer<std::uint8_t>(bytes[index]);
                if (value < 0x80)
                {
                    ++index;
                    continue;
                }

                std::size_t trailing = 0;
                if ((value & 0xE0) == 0xC0)
                {
                    trailing = 1;
                }
                else if ((value & 0xF0) == 0xE0)
                {
                    trailing = 2;
                }
                else if ((value & 0xF8) == 0xF0)
                {
                    trailing = 3;
                }
                else
                {
                    return false;
                }

                if (index + trailing >= bytes.size())
                {
                    return false;
                }

                for (std::size_t offset = 1; offset <= trailing; ++offset)
                {
                    const auto continuation = std::to_integer<std::uint8_t>(bytes[index + offset]);
                    if ((continuation & 0xC0) != 0x80)
                    {
                        return false;
                    }
                }
                index += trailing + 1;
            }
            return true;
        }

        Status DecodeCodePage(std::span<const std::byte> bytes, std::uint32_t codePage, std::wstring& text)
        {
            if (bytes.empty())
            {
                text.clear();
                return Status::Ok();
            }

            const auto source = reinterpret_cast<const char*>(bytes.data());
            const auto length = static_cast<int>(bytes.size());
            const auto wideLength = ::MultiByteToWideChar(codePage, 0, source, length, nullptr, 0);
            if (wideLength <= 0)
            {
                return Status::FromLastError(L"MultiByteToWideChar");
            }

            text.assign(static_cast<std::size_t>(wideLength), L'\0');
            if (::MultiByteToWideChar(codePage, 0, source, length, text.data(), wideLength) <= 0)
            {
                return Status::FromLastError(L"MultiByteToWideChar");
            }
            return Status::Ok();
        }
    }

    std::wstring_view ToString(Encoding encoding)
    {
        switch (encoding)
        {
        case Encoding::Utf8: return L"utf-8";
        case Encoding::Utf8Bom: return L"utf-8-bom";
        case Encoding::Utf16Le: return L"utf-16le";
        case Encoding::Utf16LeBom: return L"utf-16le-bom";
        case Encoding::Utf16Be: return L"utf-16be";
        case Encoding::Utf16BeBom: return L"utf-16be-bom";
        case Encoding::Ansi: return L"ansi";
        default: return L"unknown";
        }
    }

    bool HasBom(Encoding encoding)
    {
        return BomLength(encoding) != 0;
    }

    bool IsUnicode(Encoding encoding)
    {
        return encoding == Encoding::Utf8
            || encoding == Encoding::Utf8Bom
            || encoding == Encoding::Utf16Le
            || encoding == Encoding::Utf16LeBom
            || encoding == Encoding::Utf16Be
            || encoding == Encoding::Utf16BeBom;
    }

    Encoding Detect(std::span<const std::byte> bytes)
    {
        if (bytes.empty())
        {
            return Encoding::Unknown;
        }

        if (StartsWith(bytes, { 0xEF, 0xBB, 0xBF }))
        {
            return Encoding::Utf8Bom;
        }
        if (StartsWith(bytes, { 0xFF, 0xFE }))
        {
            return Encoding::Utf16LeBom;
        }
        if (StartsWith(bytes, { 0xFE, 0xFF }))
        {
            return Encoding::Utf16BeBom;
        }

        bool littleEndian = true;
        if (LooksLikeUtf16(bytes, littleEndian))
        {
            return littleEndian ? Encoding::Utf16Le : Encoding::Utf16Be;
        }
        if (LooksLikeUtf8(bytes))
        {
            return Encoding::Utf8;
        }
        return Encoding::Ansi;
    }

    Status DecodeWithEncoding(std::span<const std::byte> bytes, Encoding encoding, Decoded& decoded)
    {
        decoded.encoding = encoding;
        decoded.text.clear();
        decoded.codePage = 0;

        const auto bom = BomLength(encoding);
        if (bytes.size() < bom)
        {
            return Status::Failed(L"TextEncoding::Decode::TruncatedBom");
        }

        switch (encoding)
        {
        case Encoding::Utf8:
        case Encoding::Utf8Bom:
        {
            decoded.codePage = CP_UTF8;
            return DecodeCodePage(bytes.subspan(bom), CP_UTF8, decoded.text);
        }
        case Encoding::Utf16Le:
        case Encoding::Utf16LeBom:
        {
            const auto payload = bytes.subspan(bom);
            const auto count = payload.size() / sizeof(wchar_t);
            decoded.text.assign(reinterpret_cast<const wchar_t*>(payload.data()), count);
            return Status::Ok();
        }
        case Encoding::Utf16Be:
        case Encoding::Utf16BeBom:
        {
            const auto payload = bytes.subspan(bom);
            const auto count = payload.size() / sizeof(wchar_t);
            decoded.text.assign(count, L'\0');
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto high = std::to_integer<std::uint16_t>(payload[(index * 2)]);
                const auto low = std::to_integer<std::uint16_t>(payload[(index * 2) + 1]);
                decoded.text[index] = static_cast<wchar_t>((high << 8) | low);
            }
            return Status::Ok();
        }
        case Encoding::Ansi:
        {
            decoded.codePage = ::GetACP();
            return DecodeCodePage(bytes, decoded.codePage, decoded.text);
        }
        default:
        {
            return Status::Failed(L"TextEncoding::Decode::UnknownEncoding");
        }
        }
    }

    Status Decode(std::span<const std::byte> bytes, Decoded& decoded)
    {
        return DecodeWithEncoding(bytes, Detect(bytes), decoded);
    }

    std::vector<std::byte> EncodeUtf16Le(std::wstring_view text, bool withBom)
    {
        std::vector<std::byte> bytes;
        bytes.reserve((text.size() * sizeof(wchar_t)) + (withBom ? 2 : 0));

        if (withBom)
        {
            bytes.push_back(kByte(0xFF));
            bytes.push_back(kByte(0xFE));
        }

        for (const auto ch : text)
        {
            const auto value = static_cast<std::uint16_t>(ch);
            bytes.push_back(static_cast<std::byte>(value & 0x00FF));
            bytes.push_back(static_cast<std::byte>((value >> 8) & 0x00FF));
        }
        return bytes;
    }

    std::vector<std::byte> EncodeUtf8(std::wstring_view text, bool withBom)
    {
        std::vector<std::byte> bytes;
        if (withBom)
        {
            bytes.push_back(kByte(0xEF));
            bytes.push_back(kByte(0xBB));
            bytes.push_back(kByte(0xBF));
        }

        if (text.empty())
        {
            return bytes;
        }

        const auto length = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0)
        {
            return bytes;
        }

        const auto offset = bytes.size();
        bytes.resize(offset + static_cast<std::size_t>(length));
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), reinterpret_cast<char*>(bytes.data() + offset), length, nullptr, nullptr);
        return bytes;
    }
}
