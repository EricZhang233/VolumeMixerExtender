#include "Platform.h"

#include "Strings.h"

#include <windows.h>

#include <algorithm>

#include <array>

namespace vmex::platform
{
    WindowsVersion GetWindowsVersion()
    {
        WindowsVersion version;

        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        const auto module = ::GetModuleHandleW(L"ntdll.dll");
        if (module == nullptr)
        {
            return version;
        }

        const auto procedure = ::GetProcAddress(module, "RtlGetVersion");
        if (procedure == nullptr)
        {
            return version;
        }

        RTL_OSVERSIONINFOW info{};
        info.dwOSVersionInfoSize = sizeof(info);
        const auto fn = reinterpret_cast<RtlGetVersionFn>(procedure);
        if (fn(reinterpret_cast<PRTL_OSVERSIONINFOW>(&info)) != 0)
        {
            return version;
        }

        version.major = info.dwMajorVersion;
        version.minor = info.dwMinorVersion;
        version.build = info.dwBuildNumber;

        HKEY key = nullptr;
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            DWORD revision = 0;
            DWORD size = sizeof(revision);
            DWORD type = 0;
            if (::RegQueryValueExW(key, L"UBR", nullptr, &type, reinterpret_cast<LPBYTE>(&revision), &size) == ERROR_SUCCESS && type == REG_DWORD)
            {
                version.revision = revision;
            }
            ::RegCloseKey(key);
        }

        return version;
    }

    bool IsSupportedWindowsVersion(const WindowsVersion& version)
    {
        return version.build >= kMinimumWindowsBuild;
    }

    Status VerifySupportedWindowsVersion()
    {
        const auto version = GetWindowsVersion();
        if (IsSupportedWindowsVersion(version))
        {
            return Status::Ok();
        }
        return Status::Failed(DescribeWindowsVersion(version));
    }

    std::wstring DescribeWindowsVersion(const WindowsVersion& version)
    {
        std::wstring text = std::to_wstring(version.major);
        text.append(L".");
        text.append(std::to_wstring(version.minor));
        text.append(L".");
        text.append(std::to_wstring(version.build));
        text.append(L".");
        text.append(std::to_wstring(version.revision));
        return text;
    }

    std::filesystem::path GetExecutablePath()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < buffer.size())
            {
                buffer.resize(length);
                return std::filesystem::path(buffer);
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    std::filesystem::path GetExecutableDirectory()
    {
        return GetExecutablePath().parent_path();
    }

    std::filesystem::path GetLocalAppDataDirectory()
    {
        const auto length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (length == 0)
        {
            return GetExecutableDirectory();
        }

        std::wstring buffer(length, L'\0');
        ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), length);
        while (!buffer.empty() && buffer.back() == L'\0')
        {
            buffer.pop_back();
        }
        return std::filesystem::path(buffer);
    }

    std::wstring GetMachineName()
    {
        std::wstring buffer(MAX_COMPUTERNAME_LENGTH + 1, L'\0');
        DWORD size = static_cast<DWORD>(buffer.size());
        if (::GetComputerNameW(buffer.data(), &size) == FALSE)
        {
            return {};
        }
        buffer.resize(size);
        return buffer;
    }

    bool FileExists(const std::filesystem::path& file)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(file, error);
    }

    Status ReadAllBytes(const std::filesystem::path& file, std::vector<std::byte>& bytes)
    {
        bytes.clear();

        const auto handle = ::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return Status::FromLastError(L"CreateFileW");
        }

        LARGE_INTEGER size{};
        if (::GetFileSizeEx(handle, &size) == FALSE)
        {
            const auto status = Status::FromLastError(L"GetFileSizeEx");
            ::CloseHandle(handle);
            return status;
        }

        if (size.QuadPart > 0)
        {
            bytes.resize(static_cast<std::size_t>(size.QuadPart));

            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                DWORD read = 0;
                const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 20));
                if (::ReadFile(handle, bytes.data() + offset, chunk, &read, nullptr) == FALSE)
                {
                    const auto status = Status::FromLastError(L"ReadFile");
                    ::CloseHandle(handle);
                    return status;
                }
                if (read == 0)
                {
                    break;
                }
                offset += read;
            }
            bytes.resize(offset);
        }

        ::CloseHandle(handle);
        return Status::Ok();
    }

    Status WriteAllBytes(const std::filesystem::path& file, std::span<const std::byte> bytes)
    {
        const auto parent = file.parent_path();
        if (!parent.empty())
        {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
        }

        const auto handle = ::CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return Status::FromLastError(L"CreateFileW");
        }

        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            DWORD written = 0;
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 20));
            if (::WriteFile(handle, bytes.data() + offset, chunk, &written, nullptr) == FALSE)
            {
                const auto status = Status::FromLastError(L"WriteFile");
                ::CloseHandle(handle);
                return status;
            }
            offset += written;
        }

        ::CloseHandle(handle);
        return Status::Ok();
    }
}
