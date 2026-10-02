#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vmex
{
    using Code = std::int32_t;

    inline constexpr Code kCodeOk = 0;
    inline constexpr Code kCodeInvalidArguments = 1;
    inline constexpr Code kCodeNotSupported = 2;
    inline constexpr Code kCodeFailed = 3;

    struct Status final
    {
        Code code = kCodeOk;
        std::wstring detail;

        [[nodiscard]] bool IsOk() const noexcept { return code == kCodeOk; }
        explicit operator bool() const noexcept { return IsOk(); }

        static Status Ok();
        static Status Failed(std::wstring_view detail);
        static Status NotSupported(std::wstring_view detail);
        static Status InvalidArguments(std::wstring_view detail);
        static Status FromWin32(std::wstring_view context, std::uint32_t error);
        static Status FromHResult(std::wstring_view context, std::int32_t hresult);
        static Status FromLastError(std::wstring_view context);
    };

    class ITextResolver
    {
    public:
        virtual ~ITextResolver() = default;

        [[nodiscard]] virtual std::wstring Resolve(std::wstring_view key) const = 0;
        [[nodiscard]] virtual std::wstring ResolveFormat(std::wstring_view key, const std::vector<std::wstring>& arguments) const = 0;
    };
}
