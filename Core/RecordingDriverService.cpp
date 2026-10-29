#include "RecordingDriverService.h"

#include "AudioInterop.h"
#include "RecordingClient.h"
#include "Platform.h"
#include "TextService.h"
#include "Logger.h"
#include "EndpointVisibility.h"
#include "UserSettings.h"
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mscat.h>
#include <devicetopology.h>
#include <functiondiscoverykeys_devpkey.h>
#include <array>
#include <cwctype>

namespace vmex::recording
{
    namespace
    {
        constexpr GUID mediaClass{ 0x4d36e96c, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
        constexpr std::wstring_view kChannel = L"recording.driver";
        constexpr std::array<const wchar_t*, 9> packageFiles{
            L"vbMmeCable64_win10.inf", L"vbaudio_cable64_win10.cat", L"vbaudio_cable64_win10.sys",
            L"vbaudio_cable64arm_win10.sys", L"VBCABLE_ControlPanel.exe", L"VBCABLE_Setup_x64.exe",
            L"pin_in.ico", L"pin_out.ico", L"readme.txt" };

        bool HasPackage(const std::filesystem::path& directory)
        {
            for (const auto name : packageFiles)
            {
                std::error_code error;
                const auto file = directory / name;
                if (!std::filesystem::is_regular_file(file, error) || error) return false;
                const auto size = std::filesystem::file_size(file, error);
                if (error || size == 0) return false;
            }
            return true;
        }

        std::filesystem::path Package()
        {
            return platform::GetApplicationDirectory() / L"Driver" / L"VBCABLE";
        }

        std::wstring HashFile(const std::filesystem::path& path)
        {
            HCRYPTPROV provider = 0;
            HCRYPTHASH hash = 0;
            HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            std::wstring result;
            if (file != INVALID_HANDLE_VALUE && ::CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
                ::CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash))
            {
                std::array<BYTE, 65536> buffer{};
                DWORD read = 0;
                bool valid = true;
                for (;;)
                {
                    if (!::ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) { valid = false; break; }
                    if (!read) break;
                    if (!::CryptHashData(hash, buffer.data(), read, 0)) { valid = false; break; }
                }
                std::array<BYTE, 32> digest{};
                DWORD bytes = static_cast<DWORD>(digest.size());
                if (valid && ::CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &bytes, 0) && bytes == digest.size())
                {
                    constexpr wchar_t digits[] = L"0123456789ABCDEF";
                    for (const auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
                }
            }
            if (hash) ::CryptDestroyHash(hash);
            if (provider) ::CryptReleaseContext(provider, 0);
            if (file != INVALID_HANDLE_VALUE) ::CloseHandle(file);
            return result;
        }

        bool VerifyMember(const std::filesystem::path& directory, const wchar_t* name)
        {
            const auto path = directory / name;
            const auto catalog = directory / L"vbaudio_cable64_win10.cat";
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
        public:
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
                        log::Logger::Instance().WriteKeyFormat(role == audio::detail::CableEndpointRole::Unknown ?
                            log::Level::Warn : log::Level::Info, kChannel, L"log.recording.endpoint_identity",
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
                            log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kChannel,
                                vendor == CableVendor::Unknown ? L"log.recording.endpoint_vendor_unknown" :
                                    L"log.recording.endpoint_vendor_mismatch", { endpoint.friendly });
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
                    log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kChannel,
                        L"log.recording.endpoint_vendor_mismatch", { std::wstring(id) });
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
                        log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kChannel,
                            L"log.recording.endpoint_state_unconfirmed", { std::wstring(id), visible ? L"1" : L"0" });
                }
                log::Logger::Instance().WriteKeyFormat(changed ? log::Level::Info : log::Level::Warn, kChannel,
                    changed ? (visible ? L"log.recording.endpoint_shown" : L"log.recording.endpoint_hidden") :
                        L"log.recording.endpoint_failed", { std::wstring(id) });
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
            log::Logger::Instance().WriteKeyFormat(result ? log::Level::Info : log::Level::Error,
                L"recording.driver", L"log.recording.result", { std::wstring(action), std::to_wstring(result.code), result.detail });
            return result;
        }

        Status RunInstaller(bool install)
        {
            bool testSigned = false;
            const auto verified = ValidateDriverPackage(Package(), testSigned);
            if (!verified) return Report(L"driver-validate", verified);
            Snapshot recording;
            const auto stopped = Request(L"off", L"", recording);
            if (!stopped) return stopped;
            EndpointOperation operation;
            if (!operation.locked) return Report(L"driver-installer", Status::Failed(L"endpoint-operation-busy"));
            VisibilityActions actions;
            std::vector<VisibilityState> changes;
            const auto rollback = [&](Status result) {
                const auto restored = RestoreVisibility(actions, changes);
                return Report(L"driver-installer", restored ? result : Status::Failed(result.detail + L"; " + restored.detail));
            };
            if (!install)
            {
                const auto shown = ChangeVisibility(actions, true, changes);
                if (!shown) return rollback(shown);
            }
            const auto before = QueryDriver();
            const auto file = Package() / L"VBCABLE_Setup_x64.exe";
            const auto directory = Package();
            SHELLEXECUTEINFOW info{ sizeof(info) };
            info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            info.lpVerb = L"runas";
            info.lpFile = file.c_str();
            info.lpDirectory = directory.c_str();
            info.nShow = SW_SHOWNORMAL;
            if (!::ShellExecuteExW(&info)) return rollback(Status::FromLastError(L"driver-elevation"));
            const auto wait = ::WaitForSingleObject(info.hProcess, INFINITE);
            DWORD code = 0;
            const bool read = ::GetExitCodeProcess(info.hProcess, &code) != FALSE;
            ::CloseHandle(info.hProcess);
            if (wait != WAIT_OBJECT_0 || !read) return rollback(Status::Failed(L"driver-installer-process"));
            const auto state = QueryDriver();
            const bool applied = before.installed != state.installed && state.installed == install;
            if (!applied) return rollback(Status::Failed(text::Embedded().Resolve(L"driver.operation.failed") +
                L" (" + std::to_wstring(code) + L")"));
            if (install)
            {
                const auto hidden = ChangeVisibility(actions, false, changes);
                if (!hidden) return rollback(hidden);
            }
            return Report(install ? L"driver-install" : L"driver-uninstall", applied ?
                Status{ kCodeOk, text::Embedded().Resolve(L"driver.restart") } :
                Status::Failed(text::Embedded().Resolve(L"driver.operation.failed") + L" (" + std::to_wstring(code) + L")"));
        }
    }

    Status ValidateDriverPackage(const std::filesystem::path& directory, bool& testSigned)
    {
        testSigned = false;
        if (!HasPackage(directory) ||
            HashFile(directory / L"VBCABLE_Setup_x64.exe") != L"734C35DFA6D98F48782A451633CEB471166EC70D60482FD89A1123D0EE3C4F41" ||
            HashFile(directory / L"VBCABLE_ControlPanel.exe") != L"F5B44706FE7BA2EED0516DEE791F826DC7A9891E997F6CE9704E2899300B14FF" ||
            !VerifyMember(directory, L"vbaudio_cable64_win10.sys") ||
            !VerifyMember(directory, L"vbaudio_cable64arm_win10.sys") ||
            !VerifyMember(directory, L"vbMmeCable64_win10.inf"))
            return Status::Failed(text::Embedded().Resolve(L"driver.signature.invalid"));
        return Status::Ok();
    }

    DriverState QueryDriver()
    {
        DriverState state;
        state.packagePresent = HasPackage(Package());
        const HDEVINFO devices = ::SetupDiGetClassDevsW(&mediaClass, nullptr, nullptr, DIGCF_PRESENT);
        if (devices == INVALID_HANDLE_VALUE) return state;
        SP_DEVINFO_DATA device{ sizeof(device) };
        for (DWORD index = 0; ::SetupDiEnumDeviceInfo(devices, index, &device); ++index)
        {
            std::array<wchar_t, 4096> ids{};
            if (!::SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<PBYTE>(ids.data()), sizeof(ids) - sizeof(wchar_t), nullptr)) continue;
            bool match = false;
            for (const auto* id = ids.data(); *id; id += ::wcslen(id) + 1)
                match = match || IsCableHardwareId(id);
            if (!match) continue;
            state.installed = true;
            ULONG flags = 0, problem = 0;
            if (::CM_Get_DevNode_Status(&flags, &problem, device.DevInst, 0) == CR_SUCCESS)
            {
                state.started = (flags & DN_STARTED) != 0;
                state.problem = problem;
            }
            break;
        }
        ::SetupDiDestroyDeviceInfoList(devices);
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

    Status DisableCableEndpoints() { return Report(L"endpoints-disable", ApplyExtraEndpointVisibility(false)); }
    Status RestoreCableEndpoints() { return Report(L"endpoints-restore", ApplyExtraEndpointVisibility(true)); }

    Status InstallDriver() { return RunInstaller(true); }
    Status UninstallDriver() { return RunInstaller(false); }
}
