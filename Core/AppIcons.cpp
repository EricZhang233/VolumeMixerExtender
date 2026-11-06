#include "AppIcons.h"

#include "ApplicationIdentity.h"
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

        Microsoft::WRL::ComPtr<IWICBitmap> LoadAppBitmap(IWICImagingFactory& factory, const std::wstring& image,
            const std::wstring& applicationId)
        {
            auto item = application::FindRegisteredApplication(applicationId);
            if (applicationId.empty())
                ::SHCreateItemFromParsingName(image.c_str(), nullptr, IID_PPV_ARGS(&item));

            Microsoft::WRL::ComPtr<IShellItemImageFactory> images;
            if (item && SUCCEEDED(item.As(&images)))
            {
                for (const LONG edge : { 128, 64, 32 })
                {
                    HBITMAP icon = nullptr;
                    const auto result = images->GetImage(SIZE{ edge, edge }, SIIGBF_ICONONLY, &icon);
                    if (!icon) continue;
                    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
                    BITMAP dimensions{};
                    if (SUCCEEDED(result) && ::GetObjectW(icon, sizeof(dimensions), &dimensions) &&
                        dimensions.bmWidth == edge && dimensions.bmHeight == edge)
                    {
                        factory.CreateBitmapFromHBITMAP(icon, nullptr, WICBitmapUseAlpha, &bitmap);
                    }
                    ::DeleteObject(icon);
                    if (bitmap) return bitmap;
                }
            }

            if (applicationId.empty())
            {
                if (const HICON icon = LoadShellIcon(image))
                {
                    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
                    factory.CreateBitmapFromHICON(icon, &bitmap);
                    ::DestroyIcon(icon);
                    return bitmap;
                }
            }
            return {};
        }

        Status EncodePng(const std::wstring& image, const std::wstring& applicationId, const std::filesystem::path& file)
        {
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            HRESULT result = ::CoCreateInstance(
                CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
            if (FAILED(result))
            {
                return Status::FromHResult(L"wic-factory", result);
            }

            const auto bitmap = LoadAppBitmap(*factory.Get(), image, applicationId);
            if (!bitmap)
            {
                return Status::Failed(L"icon-missing");
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

        Status Extract(const std::wstring& image, const std::filesystem::path& cacheRoot, std::wstring& pngFile,
            const std::wstring& applicationId = {})
        {
            pngFile.clear();
            if (image.empty() && applicationId.empty())
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

            const auto source = applicationId.empty() ? image : L"app:" + applicationId + L"|" + image;
            const auto target = directory / (CacheKey(L"icon-v2:" + source) + L".png");
            if (platform::FileExists(target))
            {
                pngFile = target.wstring();
                return Status::Ok();
            }

            wchar_t temporary[MAX_PATH]{};
            const bool hasTemporaryFile = ::GetTempFileNameW(directory.c_str(), L"ico", 0, temporary) != 0;
            auto status = hasTemporaryFile ? EncodePng(image, applicationId, temporary) : Status::FromLastError(L"icon-temp-file");
            if (status && !::MoveFileExW(temporary, target.c_str(), 0))
            {
                const auto moveStatus = Status::FromLastError(L"icon-cache-publish");
                if (!platform::FileExists(target)) status = moveStatus;
            }
            if (hasTemporaryFile) std::filesystem::remove(temporary, error);

            if (!status.IsOk())
            {
                log::Logger::Instance().WriteFormat(
                    log::Level::Debug, kChannel, L"Application icon extraction failed source={0} detail={1}", { source, status.detail });
                return status;
            }

            log::Logger::Instance().WriteFormat(
                log::Level::Debug, kChannel, L"Application icon extracted source={0} target={1}", { source, target.wstring() });
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

        std::wstring applicationId;
        const auto image = application::ProcessImagePath(processId, nullptr, &applicationId);
        if (!applicationId.empty())
        {
            const auto status = Extract(image, cacheRoot, pngFile, applicationId);
            if (status || image.empty()) return status;
        }
        return Extract(image, cacheRoot, pngFile);
    }
}
