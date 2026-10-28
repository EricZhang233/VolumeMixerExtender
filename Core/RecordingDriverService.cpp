#include "RecordingDriverService.h"

#include "RecordingClient.h"
#include "Platform.h"
#include "TextService.h"
#include "Logger.h"
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mscat.h>
#include <algorithm>
#include <array>

namespace vmex::recording
{
    namespace
    {
        constexpr GUID mediaClass{ 0x4d36e96c, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
        constexpr wchar_t hardwareId[] = L"ROOT\\VMEX_RECORDING\0";

        struct Certificate final
        {
            PCCERT_CONTEXT value = nullptr;
            explicit Certificate(const std::filesystem::path& path)
            {
                ::CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(), CERT_QUERY_CONTENT_FLAG_CERT,
                    CERT_QUERY_FORMAT_FLAG_BINARY, 0, nullptr, nullptr, nullptr, nullptr, nullptr,
                    reinterpret_cast<const void**>(&value));
            }
            ~Certificate() { if (value) ::CertFreeCertificateContext(value); }
            bool TestSigned() const
            {
                return value && ::CertCompareCertificateName(X509_ASN_ENCODING,
                    &value->pCertInfo->Subject, &value->pCertInfo->Issuer);
            }
        };

        bool VerifySignature(WINTRUST_DATA& data, PCCERT_CONTEXT certificate, bool testSigned)
        {
            GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
            data.cbStruct = sizeof(data);
            data.dwUIChoice = WTD_UI_NONE;
            data.fdwRevocationChecks = WTD_REVOKE_NONE;
            data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
            data.dwStateAction = WTD_STATEACTION_VERIFY;
            const auto result = ::WinVerifyTrust(nullptr, &action, &data);
            bool valid = result == ERROR_SUCCESS || (testSigned && result == CERT_E_UNTRUSTEDROOT);
            auto* provider = ::WTHelperProvDataFromStateData(data.hWVTStateData);
            auto* signer = provider ? ::WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
            valid = valid && signer && signer->csCertChain &&
                certificate->cbCertEncoded == signer->pasCertChain[0].pCert->cbCertEncoded &&
                std::equal(certificate->pbCertEncoded, certificate->pbCertEncoded + certificate->cbCertEncoded,
                    signer->pasCertChain[0].pCert->pbCertEncoded);
            data.dwStateAction = WTD_STATEACTION_CLOSE;
            ::WinVerifyTrust(nullptr, &action, &data);
            return valid;
        }

        bool VerifyFile(const std::filesystem::path& path, PCCERT_CONTEXT certificate, bool testSigned)
        {
            WINTRUST_FILE_INFO file{ sizeof(file) };
            file.pcwszFilePath = path.c_str();
            WINTRUST_DATA data{};
            data.dwUnionChoice = WTD_CHOICE_FILE;
            data.pFile = &file;
            return VerifySignature(data, certificate, testSigned);
        }

        bool VerifyMember(const std::filesystem::path& catalog, const std::filesystem::path& path,
            PCCERT_CONTEXT certificate, bool testSigned)
        {
            HCATADMIN admin = nullptr;
            if (!::CryptCATAdminAcquireContext2(&admin, nullptr, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) return false;
            const auto handle = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            std::array<BYTE, 32> hash{};
            DWORD size = static_cast<DWORD>(hash.size());
            bool valid = handle != INVALID_HANDLE_VALUE &&
                ::CryptCATAdminCalcHashFromFileHandle2(admin, handle, &size, hash.data(), 0) && size == hash.size();
            if (valid)
            {
                std::wstring tag;
                constexpr wchar_t digits[] = L"0123456789ABCDEF";
                for (const auto byte : hash) { tag += digits[byte >> 4]; tag += digits[byte & 15]; }
                WINTRUST_CATALOG_INFO member{ sizeof(member) };
                member.pcwszCatalogFilePath = catalog.c_str();
                member.pcwszMemberTag = tag.c_str();
                member.pcwszMemberFilePath = path.c_str();
                member.hMemberFile = handle;
                member.pbCalculatedFileHash = hash.data();
                member.cbCalculatedFileHash = size;
                member.hCatAdmin = admin;
                WINTRUST_DATA data{};
                data.dwUnionChoice = WTD_CHOICE_CATALOG;
                data.pCatalog = &member;
                valid = VerifySignature(data, certificate, testSigned);
            }
            if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
            ::CryptCATAdminReleaseContext(admin, 0);
            return valid;
        }

        bool TestSigningEnabled()
        {
            struct Information { ULONG length; ULONG options; } info{ sizeof(Information), 0 };
            using Query = LONG(NTAPI*)(ULONG, void*, ULONG, ULONG*);
            const auto query = reinterpret_cast<Query>(::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
            return query && query(103, &info, sizeof(info), nullptr) >= 0 && (info.options & 2) != 0;
        }

        struct CertificateImport final
        {
            std::array<PCCERT_CONTEXT, 2> added{};
            bool committed = false;
            ~CertificateImport()
            {
                for (const auto certificate : added)
                    if (certificate)
                    {
                        if (committed) ::CertFreeCertificateContext(certificate);
                        else if (!::CertDeleteCertificateFromStore(certificate))
                            log::Logger::Instance().WriteKeyFormat(log::Level::Error, L"recording.driver", L"log.recording.result",
                                { L"driver-certificate-rollback", std::to_wstring(::GetLastError()), L"" });
                    }
            }
            Status Import(PCCERT_CONTEXT certificate)
            {
                constexpr std::array<const wchar_t*, 2> names{ L"ROOT", L"TrustedPublisher" };
                for (size_t index = 0; index < names.size(); ++index)
                {
                    const auto store = ::CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                        CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_OPEN_EXISTING_FLAG, names[index]);
                    if (!store) return Status::FromLastError(L"driver-certificate-store");
                    auto* existing = ::CertFindCertificateInStore(store, X509_ASN_ENCODING, 0,
                        CERT_FIND_EXISTING, certificate, nullptr);
                    bool success = true;
                    if (existing) ::CertFreeCertificateContext(existing);
                    else success = ::CertAddCertificateContextToStore(store, certificate, CERT_STORE_ADD_NEW, &added[index]) != FALSE;
                    const auto error = ::GetLastError();
                    ::CertCloseStore(store, 0);
                    if (!success) return Status::FromWin32(L"driver-certificate-import", error);
                }
                return Status::Ok();
            }
        };

        struct Devices final
        {
            HDEVINFO value = ::SetupDiGetClassDevsW(&mediaClass, nullptr, nullptr, 0);
            ~Devices() { if (value != INVALID_HANDLE_VALUE) ::SetupDiDestroyDeviceInfoList(value); }
        };

        struct Operation final
        {
            HANDLE mutex = ::CreateMutexW(nullptr, FALSE, L"Global\\Vmex.Recording.DriverOperation");
            bool locked = false;
            Operation()
            {
                if (mutex)
                {
                    const auto wait = ::WaitForSingleObject(mutex, 0);
                    locked = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
                }
            }
            ~Operation() { if (locked) ::ReleaseMutex(mutex); if (mutex) ::CloseHandle(mutex); }
        };

        bool FindDevice(HDEVINFO devices, SP_DEVINFO_DATA& device)
        {
            for (DWORD index = 0; ::SetupDiEnumDeviceInfo(devices, index, &device); ++index)
            {
                std::array<wchar_t, 4096> ids{};
                DWORD bytes = 0;
                if (!::SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_HARDWAREID, nullptr,
                    reinterpret_cast<PBYTE>(ids.data()), sizeof(ids) - sizeof(wchar_t), &bytes)) continue;
                for (const auto* id = ids.data(); *id; id += ::wcslen(id) + 1)
                    if (::_wcsicmp(id, hardwareId) == 0) return true;
            }
            return false;
        }

        std::filesystem::path Package()
        {
            return platform::GetExecutableDirectory() / L"Driver" / L"vmex_recording.inf";
        }

        bool HasPackage()
        {
            const auto directory = Package().parent_path();
            return platform::FileExists(Package()) && platform::FileExists(directory / L"vmex_recording.sys") &&
                platform::FileExists(directory / L"vmex_recording.cat") && platform::FileExists(directory / L"vmex_recording.cer");
        }

        Status Report(std::wstring_view action, Status result)
        {
            log::Logger::Instance().WriteKeyFormat(result ? log::Level::Info : log::Level::Error,
                L"recording.driver", L"log.recording.result", { std::wstring(action), std::to_wstring(result.code), result.detail });
            return result;
        }

        Status Elevated(std::wstring_view action)
        {
            const auto file = platform::GetExecutableDirectory() / L"vmex_cli.exe";
            const std::wstring arguments = L"driver " + std::wstring(action);
            SHELLEXECUTEINFOW info{ sizeof(info) };
            info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            info.lpVerb = L"runas";
            info.lpFile = file.c_str();
            info.lpParameters = arguments.c_str();
            info.nShow = SW_HIDE;
            if (!::ShellExecuteExW(&info)) return Status::FromLastError(L"driver-elevation");
            const auto wait = ::WaitForSingleObject(info.hProcess, INFINITE);
            DWORD code = kCodeFailed;
            const bool read = ::GetExitCodeProcess(info.hProcess, &code) != FALSE;
            ::CloseHandle(info.hProcess);
            if (wait != WAIT_OBJECT_0 || !read) return Status::Failed(L"driver-installer-process");
            return code == 0 ? Status::Ok() : Status::Failed(text::Embedded().Resolve(L"driver.operation.failed"));
        }

        Status RemoveDevice(HDEVINFO devices, SP_DEVINFO_DATA& device)
        {
            SP_REMOVEDEVICE_PARAMS parameters{};
            parameters.ClassInstallHeader.cbSize = sizeof(parameters.ClassInstallHeader);
            parameters.ClassInstallHeader.InstallFunction = DIF_REMOVE;
            parameters.Scope = DI_REMOVEDEVICE_GLOBAL;
            if (!::SetupDiSetClassInstallParamsW(devices, &device, &parameters.ClassInstallHeader, sizeof(parameters)) ||
                !::SetupDiCallClassInstaller(DIF_REMOVE, devices, &device)) return Status::FromLastError(L"driver-device-remove");
            return Status::Ok();
        }

        std::wstring PublishedInf(HDEVINFO devices, SP_DEVINFO_DATA& device)
        {
            std::array<wchar_t, 256> key{};
            if (!::SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_DRIVER, nullptr,
                reinterpret_cast<PBYTE>(key.data()), sizeof(key) - sizeof(wchar_t), nullptr)) return {};
            std::array<wchar_t, MAX_PATH> inf{};
            DWORD size = sizeof(inf);
            const auto path = L"SYSTEM\\CurrentControlSet\\Control\\Class\\" + std::wstring(key.data());
            if (::RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), L"InfPath", RRF_RT_REG_SZ, nullptr, inf.data(), &size) != ERROR_SUCCESS)
                return {};
            const std::wstring name(inf.data());
            if (name.size() <= 7 || name.substr(0, 3) != L"oem" || name.substr(name.size() - 4) != L".inf" ||
                !std::all_of(name.begin() + 3, name.end() - 4, [](wchar_t c) { return c >= L'0' && c <= L'9'; })) return {};
            return name;
        }
    }

    static Status ValidatePackage(const std::filesystem::path& directory, const Certificate& certificate, bool& testSigned)
    {
        testSigned = certificate.TestSigned();
        const auto catalog = directory / L"vmex_recording.cat";
        if (!certificate.value || ::CertVerifyTimeValidity(nullptr, certificate.value->pCertInfo) != 0 ||
            !VerifyFile(catalog, certificate.value, testSigned) ||
            !VerifyFile(directory / L"vmex_recording.sys", certificate.value, testSigned) ||
            !VerifyMember(catalog, directory / L"vmex_recording.sys", certificate.value, testSigned) ||
            !VerifyMember(catalog, directory / L"vmex_recording.inf", certificate.value, testSigned))
            return Status::Failed(text::Embedded().Resolve(L"driver.signature.invalid"));
        return Status::Ok();
    }

    Status ValidateDriverPackage(const std::filesystem::path& directory, bool& testSigned)
    {
        Certificate certificate(directory / L"vmex_recording.cer");
        return ValidatePackage(directory, certificate, testSigned);
    }

    DriverState QueryDriver()
    {
        DriverState state;
        state.packagePresent = HasPackage();
        Devices devices;
        SP_DEVINFO_DATA device{ sizeof(device) };
        if (devices.value == INVALID_HANDLE_VALUE || !FindDevice(devices.value, device)) return state;
        state.installed = true;
        ULONG flags = 0, problem = 0;
        if (::CM_Get_DevNode_Status(&flags, &problem, device.DevInst, 0) == CR_SUCCESS)
        {
            state.started = (flags & DN_STARTED) != 0;
            state.problem = problem;
        }
        return state;
    }

    Status InstallDriver()
    {
        if (!HasPackage()) return Report(L"driver-install", Status::NotSupported(text::Embedded().Resolve(L"driver.package.missing")));
        Certificate certificate(Package().parent_path() / L"vmex_recording.cer");
        bool testSigned = false;
        const auto verified = ValidatePackage(Package().parent_path(), certificate, testSigned);
        if (!verified) return Report(L"driver-install", verified);
        if (testSigned && !TestSigningEnabled())
            return Report(L"driver-install", Status::NotSupported(text::Embedded().Resolve(L"driver.testsigning.required")));
        if (!::IsUserAnAdmin()) return Report(L"driver-install", Elevated(L"install"));
        Operation operation;
        if (!operation.locked) return Status::Failed(L"driver-operation-busy");
        CertificateImport trust;
        if (testSigned)
        {
            const auto imported = trust.Import(certificate.value);
            if (!imported) return Report(L"driver-install", imported);
            Report(L"driver-certificate-import", Status::Ok());
        }
        Snapshot recording;
        const auto stopped = Request(L"off", L"", recording);
        if (!stopped) return stopped;
        Devices devices;
        if (devices.value == INVALID_HANDLE_VALUE) return Status::FromLastError(L"driver-device-list");
        SP_DEVINFO_DATA device{ sizeof(device) };
        const bool exists = FindDevice(devices.value, device);
        if (!exists)
        {
            if (!::SetupDiCreateDeviceInfoW(devices.value, L"VmexRecording", &mediaClass, nullptr, nullptr, DICD_GENERATE_ID, &device) ||
                !::SetupDiSetDeviceRegistryPropertyW(devices.value, &device, SPDRP_HARDWAREID,
                    reinterpret_cast<const BYTE*>(hardwareId), sizeof(hardwareId)) ||
                !::SetupDiCallClassInstaller(DIF_REGISTERDEVICE, devices.value, &device)) return Status::FromLastError(L"driver-device-register");
        }
        BOOL reboot = FALSE;
        if (!::UpdateDriverForPlugAndPlayDevicesW(nullptr, hardwareId, Package().c_str(), INSTALLFLAG_FORCE, &reboot))
        {
            auto result = Status::FromLastError(L"driver-install");
            if (!exists)
            {
                const auto removed = RemoveDevice(devices.value, device);
                if (!removed) result.detail += L"; " + removed.detail;
            }
            return Report(L"driver-install", result);
        }
        trust.committed = true;
        return Report(L"driver-install", reboot ? Status{ kCodeOk, text::Embedded().Resolve(L"driver.restart") } : Status::Ok());
    }

    Status UninstallDriver()
    {
        if (!QueryDriver().installed) return Status::Ok();
        if (!::IsUserAnAdmin()) return Report(L"driver-uninstall", Elevated(L"uninstall"));
        Operation operation;
        if (!operation.locked) return Status::Failed(L"driver-operation-busy");
        Snapshot recording;
        const auto stopped = Request(L"off", L"", recording);
        if (!stopped) return stopped;
        Devices devices;
        SP_DEVINFO_DATA device{ sizeof(device) };
        if (devices.value == INVALID_HANDLE_VALUE || !FindDevice(devices.value, device)) return Status::Ok();
        const auto inf = PublishedInf(devices.value, device);
        if (inf.empty()) return Report(L"driver-uninstall", Status::Failed(L"driver-package-identity"));
        std::array<wchar_t, MAX_PATH> windows{};
        if (!::GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()))) return Status::FromLastError(L"windows-directory");
        BOOL reboot = FALSE;
        const auto path = std::filesystem::path(windows.data()) / L"INF" / inf;
        if (!::DiUninstallDriverW(nullptr, path.c_str(), 0, &reboot)) return Report(L"driver-uninstall", Status::FromLastError(L"driver-uninstall"));
        return Report(L"driver-uninstall", reboot ? Status{ kCodeOk, text::Embedded().Resolve(L"driver.restart") } : Status::Ok());
    }
}
