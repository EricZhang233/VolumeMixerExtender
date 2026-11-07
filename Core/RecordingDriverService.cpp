#include "RecordingDriverService.h"

#include "AudioInterop.h"
#include "RecordingClient.h"
#include "Platform.h"
#include "TextService.h"
#include "Logger.h"
#include "EndpointVisibility.h"
#include "RecordingEndpointLock.h"
#include "Strings.h"
#include "UserSettings.h"
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mscat.h>
#include <devicetopology.h>
#include <functiondiscoverykeys_devpkey.h>
#include <array>
#include <set>
#include <initguid.h>
#include <devpkey.h>

namespace vmex::recording
{
    namespace
    {
        constexpr GUID mediaClass{ 0x4d36e96c, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
        constexpr std::wstring_view kChannel = L"recording.driver";
        constexpr wchar_t cableHardwareId[] = L"VBAudioVACWDM\0";
        constexpr wchar_t cableInf[] = L"vbMmeCable64_win10.inf";
        constexpr wchar_t cableCatalog[] = L"vbaudio_cable64_win10.cat";
        constexpr wchar_t driverRestartKey[] = L"SYSTEM\\CurrentControlSet\\Control\\VolumeMixerExtenderRecordingDriverRestart";
        constexpr std::array<const wchar_t*, 4> packageFiles{
            cableInf, cableCatalog, L"vbaudio_cable64_win10.sys", L"vbaudio_cable64arm_win10.sys" };

        std::span<const std::byte> EmbeddedDriverFile(std::size_t index)
        {
            HMODULE module = nullptr;
            if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&EmbeddedDriverFile), &module)) return {};
            const auto resource = ::FindResourceW(module, MAKEINTRESOURCEW(201 + index), RT_RCDATA);
            const auto loaded = resource ? ::LoadResource(module, resource) : nullptr;
            const auto* bytes = loaded ? static_cast<const std::byte*>(::LockResource(loaded)) : nullptr;
            return bytes ? std::span(bytes, ::SizeofResource(module, resource)) : std::span<const std::byte>{};
        }

        bool HasEmbeddedDriverPackage()
        {
            for (std::size_t index = 0; index < packageFiles.size(); ++index)
                if (EmbeddedDriverFile(index).empty()) return false;
            return true;
        }

        bool VerifyMember(const std::filesystem::path& directory, const wchar_t* name)
        {
            const auto path = directory / name;
            const auto catalog = directory / cableCatalog;
            HCATADMIN admin = nullptr;
            if (!::CryptCATAdminAcquireContext2(&admin, nullptr, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) return false;
            HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            std::array<BYTE, 32> hash{};
            DWORD size = static_cast<DWORD>(hash.size());
            bool valid = file != INVALID_HANDLE_VALUE &&
                ::CryptCATAdminCalcHashFromFileHandle2(admin, file, &size, hash.data(), 0) && size == hash.size();
            if (valid)
            {
                std::wstring tag;
                constexpr wchar_t digits[] = L"0123456789ABCDEF";
                for (const auto byte : hash) { tag += digits[byte >> 4]; tag += digits[byte & 15]; }
                WINTRUST_CATALOG_INFO member{ sizeof(member) };
                member.pcwszCatalogFilePath = catalog.c_str();
                member.pcwszMemberTag = tag.c_str();
                member.pcwszMemberFilePath = path.c_str();
                member.hMemberFile = file;
                member.pbCalculatedFileHash = hash.data();
                member.cbCalculatedFileHash = size;
                member.hCatAdmin = admin;
                WINTRUST_DATA data{ sizeof(data) };
                data.dwUIChoice = WTD_UI_NONE;
                data.dwUnionChoice = WTD_CHOICE_CATALOG;
                data.pCatalog = &member;
                data.dwStateAction = WTD_STATEACTION_VERIFY;
                data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
                GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
                valid = ::WinVerifyTrust(nullptr, &action, &data) == ERROR_SUCCESS;
                auto* provider = ::WTHelperProvDataFromStateData(data.hWVTStateData);
                auto* signer = provider ? ::WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
                std::array<wchar_t, 256> subject{};
                valid = valid && signer && signer->csCertChain &&
                    ::CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr,
                        subject.data(), static_cast<DWORD>(subject.size())) > 1 &&
                    std::wstring_view(subject.data()) == L"Microsoft Windows Hardware Compatibility Publisher";
                data.dwStateAction = WTD_STATEACTION_CLOSE;
                ::WinVerifyTrust(nullptr, &action, &data);
            }
            if (file != INVALID_HANDLE_VALUE) ::CloseHandle(file);
            ::CryptCATAdminReleaseContext(admin, 0);
            return valid;
        }

        enum class CableVendor { Unknown, Match, Mismatch };

        CableVendor ProbeDeviceInstance(std::wstring instance)
        {
            DEVINST device = 0;
            if (::CM_Locate_DevNodeW(&device, instance.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
                return CableVendor::Unknown;
            bool foundIdentity = false;
            for (unsigned depth = 0; depth < 16; ++depth)
            {
                std::array<wchar_t, 4096> ids{};
                ULONG bytes = sizeof(ids) - 2 * sizeof(wchar_t), type = 0;
                if (::CM_Get_DevNode_Registry_PropertyW(device, CM_DRP_HARDWAREID, &type, ids.data(), &bytes, 0) == CR_SUCCESS &&
                    type == REG_MULTI_SZ)
                {
                    foundIdentity = true;
                    for (const auto* id = ids.data(); *id; id += ::wcslen(id) + 1)
                        if (IsCableHardwareId(id)) return CableVendor::Match;
                }
                DEVINST parent = 0;
                if (::CM_Get_Parent(&parent, device, 0) != CR_SUCCESS) break;
                device = parent;
            }
            return foundIdentity ? CableVendor::Mismatch : CableVendor::Unknown;
        }

        CableVendor ProbeCableVendor(const std::wstring& endpointId)
        {
            auto enumerator = audio::detail::MakeEnumerator();
            if (!enumerator) return CableVendor::Unknown;
            audio::detail::Ptr<IMMDevice> endpoint;
            if (FAILED(enumerator->GetDevice(endpointId.c_str(), endpoint.put())) || !endpoint)
                return CableVendor::Unknown;
            audio::detail::Ptr<IPropertyStore> endpointProperties;
            std::wstring endpointInstance;
            if (SUCCEEDED(endpoint->OpenPropertyStore(STGM_READ, endpointProperties.put())) &&
                audio::detail::GetPropString(endpointProperties.get(), PKEY_Device_InstanceId, endpointInstance) &&
                ProbeDeviceInstance(endpointInstance) == CableVendor::Match) return CableVendor::Match;
            audio::detail::Ptr<IDeviceTopology> topology;
            if (FAILED(endpoint->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, topology.putv())) || !topology)
                return CableVendor::Unknown;
            audio::detail::Ptr<IConnector> connector;
            if (FAILED(topology->GetConnector(0, connector.put())) || !connector)
                return CableVendor::Unknown;
            LPWSTR connected = nullptr;
            if (FAILED(connector->GetDeviceIdConnectedTo(&connected))) return CableVendor::Unknown;
            const auto connectedId = audio::detail::TakeDeviceId(connected);
            if (connectedId.empty()) return CableVendor::Unknown;
            audio::detail::Ptr<IMMDevice> node;
            if (FAILED(enumerator->GetDevice(connectedId.c_str(), node.put())) || !node)
                return CableVendor::Unknown;
            audio::detail::Ptr<IPropertyStore> properties;
            if (FAILED(node->OpenPropertyStore(STGM_READ, properties.put())) || !properties)
                return CableVendor::Unknown;
            std::wstring instance;
            if (!audio::detail::GetPropString(properties.get(), PKEY_Device_InstanceId, instance))
                return CableVendor::Unknown;
            return ProbeDeviceInstance(instance);
        }

        struct EndpointOperation final
        {
            HANDLE mutex = ::CreateMutexW(nullptr, FALSE, L"Local\\Vmex.CableEndpointOperation");
            bool locked = false;
            EndpointOperation()
            {
                if (mutex)
                {
                    const auto wait = ::WaitForSingleObject(mutex, 0);
                    locked = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
                }
            }
            ~EndpointOperation() { if (locked) ::ReleaseMutex(mutex); if (mutex) ::CloseHandle(mutex); }
        };

        const std::wstring ownershipKey = std::wstring(settings::kRootKey) + L"\\HiddenCableEndpoints";

        class VisibilityActions final : public IVisibilityActions
        {
            bool m_diagnostics;
        public:
            explicit VisibilityActions(bool diagnostics = true) : m_diagnostics(diagnostics) {}

            Status Enumerate(std::vector<VisibilityState>& result) override
            {
                for (const auto flow : { eRender, eCapture })
                {
                    std::vector<audio::detail::Endpoint> endpoints;
                    if (!audio::detail::EnumEndpointsWithState(flow, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED, endpoints))
                        return Status::Failed(L"recording-endpoints");
                    for (const auto& endpoint : endpoints)
                    {
                        if (!audio::detail::IsOurVirtualDevice(endpoint)) continue;
                        const auto role = audio::detail::GetCableEndpointRole(endpoint);
                        std::array<wchar_t, 40> subtype{};
                        ::StringFromGUID2(endpoint.jackSubType, subtype.data(), static_cast<int>(subtype.size()));
                        if (m_diagnostics || role == audio::detail::CableEndpointRole::Unknown)
                            log::Logger::Instance().WriteFormat(role == audio::detail::CableEndpointRole::Unknown ?
                            log::Level::Warn : log::Level::Info, kChannel, L"Cable endpoint id={0} hardware={1} form={2} jack={3} role={4} state={5}",
                            { endpoint.id, endpoint.hardwareId, std::to_wstring(endpoint.formFactor), subtype.data(),
                              std::to_wstring(static_cast<int>(role)), std::to_wstring(endpoint.state) });
                        if (role == audio::detail::CableEndpointRole::Unknown) continue;
                        DWORD owned = 0, size = sizeof(owned);
                        const auto read = ::RegGetValueW(HKEY_CURRENT_USER, ownershipKey.c_str(), endpoint.id.c_str(),
                            RRF_RT_REG_DWORD, nullptr, &owned, &size);
                        if (read != ERROR_SUCCESS && read != ERROR_FILE_NOT_FOUND)
                            return Status::FromWin32(L"endpoint-ownership-read", read);
                        const auto vendor = ProbeCableVendor(endpoint.id);
                        if (vendor != CableVendor::Match)
                        {
                            log::Logger::Instance().WriteFormat(log::Level::Warn, kChannel,
                                vendor == CableVendor::Unknown ? L"Endpoint vendor identity unavailable name={0}" :
                                    L"Endpoint vendor identity mismatch name={0}", { endpoint.friendly });
                            if (owned) return Status::Failed(L"endpoint-owned-identity-unavailable");
                            continue;
                        }
                        result.push_back({ endpoint.id, (endpoint.state & DEVICE_STATE_DISABLED) == 0, owned != 0 });
                    }
                }
                return Status::Ok();
            }

            Status Store(std::wstring_view id, bool owned) override
            {
                HKEY key = nullptr;
                const auto opened = owned ? ::RegCreateKeyExW(HKEY_CURRENT_USER, ownershipKey.c_str(), 0, nullptr, 0,
                    KEY_SET_VALUE, nullptr, &key, nullptr) :
                    ::RegOpenKeyExW(HKEY_CURRENT_USER, ownershipKey.c_str(), 0, KEY_SET_VALUE, &key);
                if (!owned && opened == ERROR_FILE_NOT_FOUND) return Status::Ok();
                if (opened != ERROR_SUCCESS) return Status::FromWin32(L"endpoint-ownership-open", opened);
                const DWORD value = 1;
                const auto written = owned ? ::RegSetValueExW(key, std::wstring(id).c_str(), 0, REG_DWORD,
                    reinterpret_cast<const BYTE*>(&value), sizeof(value)) : ::RegDeleteValueW(key, std::wstring(id).c_str());
                ::RegCloseKey(key);
                return written == ERROR_SUCCESS || (!owned && written == ERROR_FILE_NOT_FOUND) ? Status::Ok() :
                    Status::FromWin32(L"endpoint-ownership-write", written);
            }

            Status SetVisible(std::wstring_view id, bool visible) override
            {
                auto enumerator = audio::detail::MakeEnumerator();
                auto device = audio::detail::FindDevice(enumerator.get(), std::wstring(id));
                audio::detail::Ptr<IMMEndpoint> endpointType;
                EDataFlow flow = eAll;
                audio::detail::Endpoint endpoint;
                if (!device || FAILED(device->QueryInterface(__uuidof(IMMEndpoint), endpointType.putv())) ||
                    FAILED(endpointType->GetDataFlow(&flow)) || !audio::detail::ReadEndpoint(device.get(), flow, endpoint) ||
                    audio::detail::GetCableEndpointRole(endpoint) == audio::detail::CableEndpointRole::Unknown ||
                    ProbeCableVendor(std::wstring(id)) != CableVendor::Match)
                {
                    log::Logger::Instance().WriteFormat(log::Level::Warn, kChannel,
                        L"Endpoint vendor identity mismatch id={0}", { std::wstring(id) });
                    return Status::Failed(L"endpoint-role-or-identity-changed");
                }
                bool changed = audio::detail::EndpointVisibilityMatches(endpoint.state, visible);
                if (!changed && audio::detail::SetEndpointVisible(std::wstring(id), visible))
                {
                    const auto deadline = ::GetTickCount64() + 1000;
                    do
                    {
                        DWORD state = 0;
                        if (SUCCEEDED(device->GetState(&state)) && audio::detail::EndpointVisibilityMatches(state, visible))
                        {
                            changed = true;
                            break;
                        }
                        ::Sleep(20);
                    } while (::GetTickCount64() < deadline);
                    if (!changed)
                        log::Logger::Instance().WriteFormat(log::Level::Warn, kChannel,
                            L"Endpoint state unconfirmed id={0} visible={1}", { std::wstring(id), visible ? L"1" : L"0" });
                }
                log::Logger::Instance().WriteFormat(changed ? log::Level::Info : log::Level::Warn, kChannel,
                    changed ? (visible ? L"Endpoint enabled id={0}" : L"Endpoint disabled id={0}") :
                        L"Endpoint visibility change failed id={0}", { std::wstring(id) });
                return changed ? Status::Ok() : Status::Failed(L"endpoint-visibility");
            }
        };

        Status ApplyExtraEndpointVisibility(bool visible)
        {
            EndpointOperation operation;
            if (!operation.locked) return Status::Failed(L"endpoint-operation-busy");
            VisibilityActions actions;
            std::vector<VisibilityState> changes;
            const auto result = ChangeVisibility(actions, visible, changes);
            if (result) return result;
            const auto restored = RestoreVisibility(actions, changes);
            return restored ? result : Status::Failed(result.detail + L"; " + restored.detail);
        }

        Status Report(std::wstring_view action, Status result)
        {
            log::Logger::Instance().WriteFormat(result ? log::Level::Info : log::Level::Error,
                L"recording.driver", L"Operation {0} code={1} detail={2}", { std::wstring(action), std::to_wstring(result.code), result.detail });
            return result;
        }

        struct CableDevices final
        {
            HDEVINFO list;
            explicit CableDevices(DWORD flags) : list(::SetupDiGetClassDevsW(&mediaClass, L"ROOT", nullptr, flags)) {}
            ~CableDevices() { if (list != INVALID_HANDLE_VALUE) ::SetupDiDestroyDeviceInfoList(list); }

            DWORD Enumerate(std::vector<SP_DEVINFO_DATA>& matches) const
            {
                if (list == INVALID_HANDLE_VALUE) return ::GetLastError();
                SP_DEVINFO_DATA device{ sizeof(device) };
                for (DWORD index = 0; ::SetupDiEnumDeviceInfo(list, index, &device); ++index)
                {
                    std::array<wchar_t, 4096> ids{};
                    DWORD type = 0;
                    if (!::SetupDiGetDeviceRegistryPropertyW(list, &device, SPDRP_HARDWAREID, &type,
                        reinterpret_cast<PBYTE>(ids.data()), sizeof(ids) - sizeof(wchar_t), nullptr) || type != REG_MULTI_SZ)
                        continue;
                    for (const auto* id = ids.data(); *id; id += ::wcslen(id) + 1)
                    {
                        if (!IsCableHardwareId(id)) continue;
                        matches.push_back(device);
                        break;
                    }
                }
                const auto error = ::GetLastError();
                return error == ERROR_NO_MORE_ITEMS ? ERROR_SUCCESS : error;
            }

            bool HasDriver(SP_DEVINFO_DATA& device) const
            {
                DWORD size = 0;
                return !::SetupDiGetDeviceRegistryPropertyW(list, &device, SPDRP_DRIVER, nullptr, nullptr, 0, &size) &&
                    ::GetLastError() == ERROR_INSUFFICIENT_BUFFER && size > sizeof(wchar_t);
            }
        };

        struct DriverCache final
        {
            std::filesystem::path path;

            DWORD Create()
            {
                std::array<wchar_t, MAX_PATH> windows{};
                const auto length = ::GetSystemWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
                if (!length) return ::GetLastError();
                if (length >= windows.size()) return ERROR_FILENAME_EXCED_RANGE;
                const auto root = std::filesystem::path(windows.data()) / L"SystemTemp";
                const auto attributes = ::GetFileAttributesW(root.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES) return ::GetLastError();
                if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return ERROR_DIRECTORY;
                GUID guid{};
                if (FAILED(::CoCreateGuid(&guid))) return ERROR_GEN_FAILURE;
                std::array<wchar_t, 40> id{};
                ::StringFromGUID2(guid, id.data(), static_cast<int>(id.size()));
                auto directory = root / (L"Vmex.Driver." + std::wstring(id.data()));
                if (!::CreateDirectoryW(directory.c_str(), nullptr)) return ::GetLastError();
                path = std::move(directory);
                return ERROR_SUCCESS;
            }

            ~DriverCache()
            {
                if (path.empty()) return;
                for (const auto* name : packageFiles) ::DeleteFileW((path / name).c_str());
                ::RemoveDirectoryW(path.c_str());
            }
        };

        DWORD CheckDriverRestart()
        {
            HKEY key = nullptr;
            const auto status = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, driverRestartKey, 0, KEY_QUERY_VALUE, &key);
            if (status == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
            if (status != ERROR_SUCCESS) return static_cast<DWORD>(status);
            ::RegCloseKey(key);
            return ERROR_FAIL_REBOOT_REQUIRED;
        }

        struct DriverRestartBarrier final
        {
            bool armed = false;

            DWORD Arm()
            {
                HKEY key = nullptr;
                DWORD disposition = 0;
                const auto status = ::RegCreateKeyExW(HKEY_LOCAL_MACHINE, driverRestartKey, 0, nullptr,
                    REG_OPTION_VOLATILE, KEY_QUERY_VALUE, nullptr, &key, &disposition);
                if (status != ERROR_SUCCESS) return static_cast<DWORD>(status);
                ::RegCloseKey(key);
                armed = disposition == REG_CREATED_NEW_KEY;
                return armed ? ERROR_SUCCESS : ERROR_FAIL_REBOOT_REQUIRED;
            }

            DWORD Complete(DWORD result) const
            {
                if (!armed || result != ERROR_SUCCESS) return result;
                const auto status = ::RegDeleteKeyW(HKEY_LOCAL_MACHINE, driverRestartKey);
                return status == ERROR_SUCCESS ? ERROR_SUCCESS : ERROR_SUCCESS_REBOOT_REQUIRED;
            }
        };

        DWORD InstallEmbeddedDriver(DriverRestartBarrier& restartBarrier)
        {
            CableDevices devices(DIGCF_PRESENT);
            std::vector<SP_DEVINFO_DATA> matches;
            if (const auto error = devices.Enumerate(matches); error != ERROR_SUCCESS) return error;
            for (auto& device : matches)
                if (devices.HasDriver(device)) return ERROR_SUCCESS;

            DriverCache cache;
            if (const auto error = cache.Create(); error != ERROR_SUCCESS) return error;
            if (!ExtractDriverPackage(cache.path)) return ERROR_WRITE_FAULT;
            if (!ValidateDriverPackage(cache.path)) return static_cast<DWORD>(TRUST_E_BAD_DIGEST);
            const auto inf = cache.path / cableInf;
            if (const auto error = restartBarrier.Arm(); error != ERROR_SUCCESS) return error;

            SP_DEVINFO_DATA created{ sizeof(created) };
            if (matches.empty())
            {
                if (!::SetupDiCreateDeviceInfoW(devices.list, cableHardwareId, &mediaClass, nullptr, nullptr, DICD_GENERATE_ID, &created) ||
                    !::SetupDiSetDeviceRegistryPropertyW(devices.list, &created, SPDRP_HARDWAREID,
                        reinterpret_cast<const BYTE*>(cableHardwareId), sizeof(cableHardwareId)) ||
                    !::SetupDiCallClassInstaller(DIF_REGISTERDEVICE, devices.list, &created)) return ::GetLastError();
            }

            BOOL restart = FALSE;
            if (::UpdateDriverForPlugAndPlayDevicesW(nullptr, cableHardwareId, inf.c_str(), INSTALLFLAG_NONINTERACTIVE, &restart))
                return restart ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_SUCCESS;
            const auto error = ::GetLastError();
            if (error == ERROR_NO_MORE_ITEMS && (matches.empty() ? devices.HasDriver(created) : devices.HasDriver(matches.front())))
                return ERROR_SUCCESS;
            if (matches.empty() && !::DiUninstallDevice(nullptr, devices.list, &created, 0, &restart)) return ::GetLastError();
            return error;
        }

        DWORD RemoveCableDriver(DriverRestartBarrier& restartBarrier)
        {
            CableDevices devices(0);
            std::vector<SP_DEVINFO_DATA> matches;
            if (const auto error = devices.Enumerate(matches); error != ERROR_SUCCESS) return error;
            if (matches.empty()) return ERROR_SUCCESS;
            if (const auto error = restartBarrier.Arm(); error != ERROR_SUCCESS) return error;
            std::set<std::wstring> packages;
            for (auto& device : matches)
            {
                std::array<wchar_t, MAX_PATH> inf{};
                DEVPROPTYPE type = 0;
                if (::SetupDiGetDevicePropertyW(devices.list, &device, &DEVPKEY_Device_DriverInfPath, &type,
                    reinterpret_cast<PBYTE>(inf.data()), sizeof(inf), nullptr, 0) && type == DEVPROP_TYPE_STRING)
                {
                    const auto name = strings::ToLower(inf.data());
                    if (name.starts_with(L"oem") && name.ends_with(L".inf") && name.size() > 7 &&
                        name.find_first_not_of(L"0123456789", 3) == name.size() - 4) packages.insert(name);
                }
                BOOL deviceRestart = FALSE;
                if (!::DiUninstallDevice(nullptr, devices.list, &device, 0, &deviceRestart)) return ::GetLastError();
            }
            for (const auto& name : packages)
            {
                if (::SetupUninstallOEMInfW(name.c_str(), 0, nullptr)) continue;
                const auto error = ::GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_INF_IN_USE_BY_DEVICES) continue;
                return error;
            }
            return ERROR_SUCCESS_REBOOT_REQUIRED;
        }

        Status ProtectCableDefaults();

        Status RunDriverOperation(bool install)
        {
            const auto action = install ? L"driver-install" : L"driver-uninstall";
            const auto restart = CheckDriverRestart();
            if (restart == ERROR_FAIL_REBOOT_REQUIRED)
                return Report(action, Status::Failed(text::Embedded().Resolve(L"driver.restart.required")));
            if (restart != ERROR_SUCCESS) return Report(action, Status::FromWin32(L"driver-restart-check", restart));
            if (install && !HasEmbeddedDriverPackage())
                return Report(action, Status::Failed(text::Embedded().Resolve(L"driver.package.missing")));
            Snapshot recording;
            const auto stopped = Request(L"off", L"", recording);
            if (!stopped) return stopped;
            EndpointOperation operation;
            if (!operation.locked) return Report(action, Status::Failed(L"endpoint-operation-busy"));
            VisibilityActions actions;
            const auto result = MaintainDriverVisibility(actions, install, [&]() -> Status
            {
                const auto directory = platform::GetApplicationDirectory();
                const auto file = directory / L"vmex.exe";
                SHELLEXECUTEINFOW info{ sizeof(info) };
                info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
                info.lpVerb = L"runas";
                info.lpFile = file.c_str();
                info.lpParameters = install ? kDriverInstallArgument.data() : kDriverRemoveArgument.data();
                info.lpDirectory = directory.c_str();
                info.nShow = SW_HIDE;
                if (!::ShellExecuteExW(&info)) return Status::FromLastError(L"driver-elevation");
                const auto wait = ::WaitForSingleObject(info.hProcess, INFINITE);
                DWORD code = ERROR_GEN_FAILURE;
                const bool read = wait == WAIT_OBJECT_0 && ::GetExitCodeProcess(info.hProcess, &code);
                ::CloseHandle(info.hProcess);
                if (!read) return Status::Failed(L"driver-process");
                if (code == ERROR_FAIL_REBOOT_REQUIRED)
                    return Status::Failed(text::Embedded().Resolve(L"driver.restart.required"));
                if (code == static_cast<DWORD>(TRUST_E_BAD_DIGEST))
                    return Status::Failed(text::Embedded().Resolve(L"driver.signature.invalid"));
                if (code != ERROR_SUCCESS && code != ERROR_SUCCESS_REBOOT_REQUIRED)
                    return Status::Failed(text::Embedded().ResolveFormat(L"driver.operation.failed", { std::to_wstring(code) }));
                const auto protectedDefaults = ProtectCableDefaults();
                if (!protectedDefaults) return protectedDefaults;
                return { kCodeOk, text::Embedded().Resolve(
                    code == ERROR_SUCCESS_REBOOT_REQUIRED ? L"driver.restart" : L"driver.done") };
            }, [] { return CheckDriverRestart() == ERROR_FAIL_REBOOT_REQUIRED; });
            return Report(action, result);
        }
    }

    Status ExtractDriverPackage(const std::filesystem::path& directory)
    {
        if (!HasEmbeddedDriverPackage()) return Status::Failed(text::Embedded().Resolve(L"driver.package.missing"));
        for (std::size_t index = 0; index < packageFiles.size(); ++index)
        {
            const auto written = platform::WriteAllBytes(directory / packageFiles[index], EmbeddedDriverFile(index));
            if (!written) return written;
        }
        return Status::Ok();
    }

    Status ValidateDriverPackage(const std::filesystem::path& directory)
    {
        for (const auto* name : packageFiles)
            if (name != cableCatalog && !VerifyMember(directory, name))
                return Status::Failed(text::Embedded().Resolve(L"driver.signature.invalid"));
        return Status::Ok();
    }

    DriverState QueryDriver()
    {
        DriverState state;
        state.packagePresent = HasEmbeddedDriverPackage();
        state.restartRequired = CheckDriverRestart() == ERROR_FAIL_REBOOT_REQUIRED;
        CableDevices devices(DIGCF_PRESENT);
        std::vector<SP_DEVINFO_DATA> matches;
        if (devices.Enumerate(matches) != ERROR_SUCCESS) return state;
        for (auto& device : matches)
        {
            if (!devices.HasDriver(device)) continue;
            state.installed = true;
            ULONG flags = 0, problem = 0;
            if (::CM_Get_DevNode_Status(&flags, &problem, device.DevInst, 0) == CR_SUCCESS)
            {
                state.started = (flags & DN_STARTED) != 0;
                state.problem = problem;
            }
            if (state.started && !state.problem) break;
        }
        return state;
    }

    Status QueryRecordingReadiness(std::wstring_view monitorDevice)
    {
        struct Apartment final
        {
            HRESULT result = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ~Apartment() { if (SUCCEEDED(result)) ::CoUninitialize(); }
        } apartment;
        if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE)
            return Status::FromHResult(L"recording-readiness-com", apartment.result);
        const auto driver = QueryDriver();
        if (driver.restartRequired) return Status::NotSupported(text::Embedded().Resolve(L"driver.restart.required"));
        if (!driver.installed || !driver.started || driver.problem)
            return Status::NotSupported(text::Embedded().Resolve(driver.installed ? L"recording.driver.notReady" : L"recording.driver.missing"));
        std::wstring virtualId, friendly;
        if (!audio::detail::FindVirtualEndpoint(virtualId, friendly, true) || ProbeCableVendor(virtualId) != CableVendor::Match)
            return Status::NotSupported(text::Embedded().Resolve(L"recording.endpoint.notReady"));
        auto enumerator = audio::detail::MakeEnumerator();
        if (!enumerator) return Status::Failed(L"audio-enumerator");
        std::wstring monitor(monitorDevice);
        if (monitor.empty() && !audio::detail::GetDefaultEndpointId(eRender, monitor))
            return Status::NotSupported(text::Embedded().Resolve(L"recording.monitor.physicalRequired"));
        auto output = audio::detail::FindDevice(enumerator.get(), monitor);
        audio::detail::Endpoint endpoint;
        if (!output || !audio::detail::ReadEndpoint(output.get(), eRender, endpoint) ||
            endpoint.state != DEVICE_STATE_ACTIVE || audio::detail::IsOurVirtualDevice(endpoint))
            return Status::NotSupported(text::Embedded().Resolve(L"recording.monitor.physicalRequired"));
        audio::detail::Ptr<IMMEndpoint> flow;
        EDataFlow direction = eAll;
        if (FAILED(output->QueryInterface(__uuidof(IMMEndpoint), flow.putv())) ||
            FAILED(flow->GetDataFlow(&direction)) || direction != eRender)
            return Status::NotSupported(text::Embedded().Resolve(L"recording.monitor.physicalRequired"));
        for (const auto& id : { monitor })
        {
            auto device = audio::detail::FindDevice(enumerator.get(), id);
            audio::detail::Ptr<IAudioClient> client;
            if (!device || FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.putv())))
                return Status::NotSupported(text::Embedded().Resolve(L"recording.format.notReady"));
            WAVEFORMATEX* format = nullptr;
            const auto result = client->GetMixFormat(&format);
            const bool valid = SUCCEEDED(result) && format && format->nSamplesPerSec && format->nBlockAlign && format->nChannels;
            ::CoTaskMemFree(format);
            if (!valid) return Status::NotSupported(text::Embedded().Resolve(L"recording.format.notReady"));
        }
        return Status::Ok();
    }

    Status SetRecordingEndpointEnabled(std::wstring_view id, bool enabled)
    {
        if (id.empty()) return Status::Ok();
        EndpointOperation operation;
        if (!operation.locked) return Report(L"recording-endpoint", Status::Failed(L"endpoint-operation-busy"));
        auto enumerator = audio::detail::MakeEnumerator();
        auto device = audio::detail::FindDevice(enumerator.get(), std::wstring(id));
        audio::detail::Endpoint endpoint;
        if (!device || !audio::detail::ReadEndpoint(device.get(), eRender, endpoint) ||
            audio::detail::GetCableEndpointRole(endpoint) != audio::detail::CableEndpointRole::Playback)
            return Report(L"recording-endpoint", Status::Failed(L"recording-endpoint-identity"));
        VisibilityActions actions;
        return Report(enabled ? L"recording-endpoint-enable" : L"recording-endpoint-disable", actions.SetVisible(id, enabled));
    }

    Status DisableCableEndpoints()
    {
        if (CheckDriverRestart() == ERROR_FAIL_REBOOT_REQUIRED) return ProtectPendingDriverDefaults();
        return Report(L"endpoints-disable", ApplyExtraEndpointVisibility(false));
    }
    Status RestoreCableEndpoints()
    {
        if (CheckDriverRestart() == ERROR_FAIL_REBOOT_REQUIRED) return ProtectPendingDriverDefaults();
        return Report(L"endpoints-restore", ApplyExtraEndpointVisibility(true));
    }

    Status ProtectPendingDriverDefaults()
    {
        const auto restart = CheckDriverRestart();
        if (restart == ERROR_SUCCESS) return Status::Ok();
        if (restart != ERROR_FAIL_REBOOT_REQUIRED) return Status::FromWin32(L"driver-restart-check", restart);
        return ProtectCableDefaults();
    }

    Status MaintainCableStandby(bool recordingActive)
    {
        if (recordingActive) return Status::Ok();
        EndpointOperation operation;
        if (!operation.locked) return Status::Ok();
        const auto restart = CheckDriverRestart();
        if (restart != ERROR_SUCCESS && restart != ERROR_FAIL_REBOOT_REQUIRED)
            return Status::FromWin32(L"driver-restart-check", restart);
        VisibilityActions actions(false);
        return MaintainStandbyVisibility(actions, false, restart == ERROR_FAIL_REBOOT_REQUIRED, ProtectCableDefaults);
    }

    namespace
    {
        Status ProtectCableDefaults()
        {
            std::vector<audio::detail::Endpoint> outputs;
            if (!audio::detail::EnumEndpoints(eRender, outputs, true)) return Status::Failed(L"driver-output-enumeration");
            std::vector<std::wstring> virtualEndpoints, physicalEndpoints;
            for (const auto& endpoint : outputs)
                (audio::detail::IsOurVirtualDevice(endpoint) ? virtualEndpoints : physicalEndpoints).push_back(endpoint.id);
            struct Defaults final : IOutputDefaultActions
            {
                Status ReadDefault(unsigned role, std::wstring& endpoint) override
                {
                    auto enumerator = audio::detail::MakeEnumerator();
                    if (!enumerator) return Status::Failed(L"driver-default-enumerator");
                    audio::detail::Ptr<IMMDevice> device;
                    auto result = enumerator->GetDefaultAudioEndpoint(eRender, static_cast<ERole>(role), device.put());
                    LPWSTR id = nullptr;
                    if (SUCCEEDED(result)) result = device->GetId(&id);
                    endpoint = audio::detail::TakeDeviceId(id);
                    return SUCCEEDED(result) ? Status::Ok() : Status::FromHResult(L"driver-default-query", result);
                }
                Status SetDefault(unsigned role, std::wstring_view endpoint) override
                {
                    const auto result = audio::detail::SetSystemDefaultEndpointForRoleResult(std::wstring(endpoint), static_cast<ERole>(role));
                    return SUCCEEDED(result) ? Status::Ok() : Status::FromHResult(L"driver-default-restore", result);
                }
            } actions;
            return ProtectPhysicalDefaults(actions, virtualEndpoints, physicalEndpoints);
        }
    }

    Status InstallDriver() { return RunDriverOperation(true); }
    Status UninstallDriver() { return RunDriverOperation(false); }

    int RunDriverMaintenance(bool install)
    {
        if (!::IsUserAnAdmin()) return ERROR_ELEVATION_REQUIRED;
        const HANDLE mutex = ::CreateMutexW(nullptr, FALSE, L"Global\\Vmex.CableDriverOperation");
        if (!mutex) return static_cast<int>(::GetLastError());
        const auto wait = ::WaitForSingleObject(mutex, 0);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
        {
            ::CloseHandle(mutex);
            return ERROR_BUSY;
        }
        DWORD result = CheckDriverRestart();
        if (result == ERROR_SUCCESS)
        {
            DriverRestartBarrier restartBarrier;
            result = ERROR_GEN_FAILURE;
            try { result = install ? InstallEmbeddedDriver(restartBarrier) : RemoveCableDriver(restartBarrier); }
            catch (...) {}
            result = restartBarrier.Complete(result);
        }
        ::ReleaseMutex(mutex);
        ::CloseHandle(mutex);
        return static_cast<int>(result);
    }
}
