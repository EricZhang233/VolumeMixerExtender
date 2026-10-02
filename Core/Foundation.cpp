#include "Foundation.h"

#include <windows.h>

namespace vmex
{
    namespace
    {
        std::wstring Compose(std::wstring_view context, std::wstring_view tail)
        {
            std::wstring text(context);
            if (!text.empty() && !tail.empty())
            {
                text.append(L": ");
            }
            text.append(tail);
            return text;
        }

        std::wstring LastErrorText(std::uint32_t error)
        {
            wchar_t* buffer = nullptr;
            const auto length = ::FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                error,
                0,
                reinterpret_cast<wchar_t*>(&buffer),
                0,
                nullptr);

            std::wstring text;
            if (length != 0 && buffer != nullptr)
            {
                text.assign(buffer, length);
                while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
                {
                    text.pop_back();
                }
            }
            if (buffer != nullptr)
            {
                ::LocalFree(buffer);
            }
            return text;
        }
    }

    Status Status::Ok()
    {
        return Status{};
    }

    Status Status::Failed(std::wstring_view detail)
    {
        Status status;
        status.code = kCodeFailed;
        status.detail.assign(detail);
        return status;
    }

    Status Status::NotSupported(std::wstring_view detail)
    {
        Status status;
        status.code = kCodeNotSupported;
        status.detail.assign(detail);
        return status;
    }

    Status Status::InvalidArguments(std::wstring_view detail)
    {
        Status status;
        status.code = kCodeInvalidArguments;
        status.detail.assign(detail);
        return status;
    }

    Status Status::FromWin32(std::wstring_view context, std::uint32_t error)
    {
        Status status;
        status.code = kCodeFailed;
        status.detail = Compose(context, LastErrorText(error));
        return status;
    }

    Status Status::FromHResult(std::wstring_view context, std::int32_t hresult)
    {
        Status status;
        status.code = kCodeFailed;
        status.detail = Compose(context, LastErrorText(static_cast<std::uint32_t>(hresult)));
        return status;
    }

    Status Status::FromLastError(std::wstring_view context)
    {
        return FromWin32(context, ::GetLastError());
    }
}
