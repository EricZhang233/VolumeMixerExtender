#include "AppIcons.h"

#include "Logger.h"
#include "Platform.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <wincodec.h>

#include <wrl/client.h>

#include <cwctype>

namespace vmex::icons
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"icons";
        constexpr std::wstring_view kIconsDirectory = L"icons";

        std::wstring CacheKey(std::wstring_view path)
        {
            std::uint64_t hash = 1469598103934665603ull;
            for (const wchar_t value : path)
            {
                hash ^= static_cast<std::uint64_t>(std::towlower(value));
                hash *= 1099511628211ull;
            }

            wchar_t buffer[32] = {};
            ::swprintf_s(buffer, L"%016llX", hash);
            return buffer;
        }

        std::wstring ProcessImagePath(std::uint32_t processId)
        {
            const HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
            if (process == nullptr)
            {
                return {};
            }

            std::wstring buffer(MAX_PATH, L'\0');
            for (;;)
            {
                DWORD size = static_cast<DWORD>(buffer.size());
                if (::QueryFullProcessImageNameW(process, 0, buffer.data(), &size) != FALSE)
                {
                    buffer.resize(size);
                    ::CloseHandle(process);
                    return buffer;
                }

                if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                {
                    break;
                }
                buffer.resize(buffer.size() * 2);
            }

            ::CloseHandle(process);
            return {};
        }

        HICON LoadShellIcon(const std::wstring& image)
        {
            SHFILEINFOW info{};
            if (::SHGetFileInfoW(image.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON) != 0 &&
                info.hIcon != nullptr)
            {
                return info.hIcon;
            }

            HICON large = nullptr;
            if (::ExtractIconExW(image.c_str(), 0, &large, nullptr, 1) > 0 && large != nullptr)
            {
                return large;
            }

            return nullptr;
        }

        Status EncodePng(HICON icon, const std::filesystem::path& file)
        {
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            HRESULT result = ::CoCreateInstance(
                CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-factory", result);
            }

            Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
            result = factory->CreateBitmapFromHICON(icon, &bitmap);
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-bitmap", result);
            }

            Microsoft::WRL::ComPtr<IWICStream> stream;
            result = factory->CreateStream(&stream);
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-stream", result);
            }

            result = stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE);
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-file", result);
            }

            Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
            result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-encoder", result);
            }

            result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-encoder-init", result);
            }

            Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
            Microsoft::WRL::ComPtr<IPropertyBag2> properties;
            result = encoder->CreateNewFrame(&frame, &properties);
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-frame", result);
            }

            result = frame->Initialize(properties.Get());
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-frame-init", result);
            }

            result = frame->WriteSource(bitmap.Get(), nullptr);
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-write", result);
            }

            result = frame->Commit();
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-frame-commit", result);
            }

            result = encoder->Commit();
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-commit", result);
            }

            return Status::Ok();
        }

        Status Extract(const std::wstring& image, const std::filesystem::path& cacheRoot, std::wstring& pngFile)
        {
            pngFile.clear();
            if (image.empty())
            {
                return Status::Failed(L"icon-no-image");
            }

            const auto directory = cacheRoot / std::wstring(kIconsDirectory);
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error)
            {
                return Status::Failed(L"icon-directory");
            }

            const auto target = directory / (CacheKey(image) + L".png");
            if (platform::FileExists(target))
            {
                pngFile = target.wstring();
                return Status::Ok();
            }

            const HICON icon = LoadShellIcon(image);
            if (icon == nullptr)
            {
                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Debug, kChannel, L"log.icons.missing", { image });
                return Status::Failed(L"icon-missing");
            }

            const auto status = EncodePng(icon, target);
            ::DestroyIcon(icon);

            if (!status.IsOk())
            {
                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Debug, kChannel, L"log.icons.encode_failed", { image, status.detail });
                std::filesystem::remove(target, error);
                return status;
            }

            log::Logger::Instance().WriteKeyFormat(
                log::Level::Debug, kChannel, L"log.icons.extracted", { image, target.wstring() });
            pngFile = target.wstring();
            return Status::Ok();
        }
    }

    Status FileForImage(const std::filesystem::path& image, const std::filesystem::path& cacheRoot, std::wstring& pngFile)
    {
        return Extract(image.wstring(), cacheRoot, pngFile);
    }

    Status FileForProcess(std::uint32_t processId, const std::filesystem::path& cacheRoot, std::wstring& pngFile)
    {
        if (processId == 0)
        {
            pngFile.clear();
            return Status::Failed(L"icon-no-process");
        }

        return Extract(ProcessImagePath(processId), cacheRoot, pngFile);
    }
}
