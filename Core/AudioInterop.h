#pragma once

#include <windows.h>
#include <winver.h>
#include <winstring.h>
#include <inspectable.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <audiopolicy.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <atomic>
#include <functional>
#include <limits>
#include <memory>

#include "Logger.h"
#include "TextService.h"

namespace vmex::audio::detail {

static const PROPERTYKEY kPkeyFriendlyName =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };
static const PROPERTYKEY kPkeyDeviceDesc =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 2 };
static const PROPERTYKEY kPkeyEnumeratorName =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 24 };
static const PROPERTYKEY kPkeyInterfaceName =
    { { 0x026e516e, 0xb814, 0x414b, { 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22 } }, 2 };

template <class T>
class Ptr
{
public:
    Ptr() = default;
    ~Ptr() { reset(); }
    Ptr(const Ptr&) = delete;
    Ptr& operator=(const Ptr&) = delete;
    Ptr(Ptr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    Ptr& operator=(Ptr&& o) noexcept
    {
        if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    void attach(T* p) { reset(); p_ = p; }
    T** put() { reset(); return &p_; }
    void** putv() { reset(); return reinterpret_cast<void**>(&p_); }
private:
    T* p_ = nullptr;
};

class __declspec(uuid("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9")) PolicyConfigClient;

struct __declspec(uuid("F8679F50-850A-41CF-9C72-430F290290C8")) IPolicyConfig : ::IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Unused1() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused2() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused3() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused4() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused5() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused6() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused7() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused8() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR wszDeviceId, const PROPERTYKEY* pkey, PROPVARIANT* pv) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR wszDeviceId, const PROPERTYKEY* pkey, const PROPVARIANT* pv) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR wszDeviceId, ERole eRole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR wszDeviceId, short isVisible) = 0;
};

struct __declspec(uuid("ab3d4648-e242-459f-b02f-541c70306324")) IAudioPolicyConfigFactory21H2 : ::IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE Unused01() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused02() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused03() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused04() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused05() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused06() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused07() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused08() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused09() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused10() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused11() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused12() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused13() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused14() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused15() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused16() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused17() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused18() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused19() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPersistedDefaultAudioEndpoint(UINT32 processId, EDataFlow flow, ERole role, HSTRING deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPersistedDefaultAudioEndpoint(UINT32 processId, EDataFlow flow, ERole role, HSTRING* deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAllPersistedApplicationDefaultEndpoints() = 0;
};

struct __declspec(uuid("2a59116d-6c4f-45e0-a74f-707e3fef9258")) IAudioPolicyConfigFactoryDownlevel
    : public IAudioPolicyConfigFactory21H2
{
};

struct Endpoint
{
    std::wstring id;
    std::wstring friendly;
    std::wstring interfaceName;
    std::wstring enumerator;
    EDataFlow    flow = eRender;
    bool         isDefault = false;
};

struct Session
{
    std::wstring id;
    std::wstring key;
    std::wstring name;
    DWORD        pid = 0;
    bool         systemSounds = false;
    float        volume = 1.0f;
    bool         mute = false;
    AudioSessionState state = AudioSessionStateInactive;
    Ptr<ISimpleAudioVolume> vol;
};

inline void CoInitOnce()
{
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
}

inline std::wstring TakeDeviceId(LPWSTR p)
{
    if (!p) return std::wstring();
    std::wstring s(p);
    ::CoTaskMemFree(p);
    return s;
}

inline bool GetPropString(IPropertyStore* store, const PROPERTYKEY& key, std::wstring& out)
{
    out.clear();
    PROPVARIANT pv{};
    bool ok = false;
    if (SUCCEEDED(store->GetValue(key, &pv))) {
        if (pv.vt == VT_LPWSTR && pv.pwszVal) { out = pv.pwszVal; ok = true; }
    }
    ::PropVariantClear(&pv);
    return ok;
}

inline Ptr<IMMDeviceEnumerator> MakeEnumerator()
{
    Ptr<IMMDeviceEnumerator> e;
    ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                       __uuidof(IMMDeviceEnumerator), e.putv());
    return e;
}

inline Ptr<IMMDevice> FindDevice(IMMDeviceEnumerator* e, const std::wstring& id)
{
    Ptr<IMMDevice> d;
    if (!e || id.empty()) return d;
    e->GetDevice(id.c_str(), d.put());
    return d;
}

inline std::wstring ShortDeviceName(const std::wstring& friendly)
{
    if (friendly.empty() || friendly.back() != L')') return friendly;
    const size_t open = friendly.rfind(L" (");
    if (open == std::wstring::npos || open == 0) return friendly;
    return friendly.substr(0, open);
}

inline std::wstring DisplayDeviceName(const Endpoint& ep, bool showDriverName)
{
    return showDriverName ? ep.friendly : ShortDeviceName(ep.friendly);
}

inline bool IsOurVirtualDevice(const Endpoint& ep)
{
    if (ep.friendly.find(L"OC Virtual") != std::wstring::npos) return true;
    if (ep.friendly.find(L"OCVirtual") != std::wstring::npos) return true;
    return false;
}

inline bool ReadEndpoint(IMMDevice* dev, EDataFlow flow, Endpoint& out)
{
    out.flow = flow;
    out.id = TakeDeviceId([&] { LPWSTR p = nullptr; dev->GetId(&p); return p; }());
    if (out.id.empty()) return false;

    Ptr<IPropertyStore> props;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, props.put()))) {
        GetPropString(props.get(), kPkeyFriendlyName, out.friendly);
        GetPropString(props.get(), kPkeyInterfaceName, out.interfaceName);
        GetPropString(props.get(), kPkeyEnumeratorName, out.enumerator);
    }
    if (out.friendly.empty()) out.friendly = out.id;
    return true;
}

inline bool GetDefaultEndpointId(EDataFlow flow, std::wstring& out)
{
    out.clear();
    auto e = MakeEnumerator();
    if (!e) return false;
    Ptr<IMMDevice> dev;
    if (FAILED(e->GetDefaultAudioEndpoint(flow, eMultimedia, dev.put())) || !dev) return false;
    LPWSTR p = nullptr;
    if (FAILED(dev->GetId(&p))) return false;
    out = TakeDeviceId(p);
    return !out.empty();
}

inline bool EnumEndpointsOnce(EDataFlow flow, std::vector<Endpoint>& out)
{
    out.clear();
    auto e = MakeEnumerator();
    if (!e) return false;

    Ptr<IMMDeviceCollection> coll;
    if (FAILED(e->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, coll.put())) || !coll) return false;

    UINT count = 0;
    if (FAILED(coll->GetCount(&count))) return false;
    for (UINT i = 0; i < count; ++i) {
        Ptr<IMMDevice> dev;
        if (FAILED(coll->Item(i, dev.put())) || !dev) continue;
        Endpoint ep;
        if (!ReadEndpoint(dev.get(), flow, ep)) continue;
        if (IsOurVirtualDevice(ep)) continue;
        out.push_back(std::move(ep));
    }

    std::wstring def;
    if (GetDefaultEndpointId(flow, def)) {
        for (auto& ep : out) ep.isDefault = (ep.id == def);
    }
    return true;
}

inline bool EnumEndpoints(EDataFlow flow, std::vector<Endpoint>& out)
{
    if (EnumEndpointsOnce(flow, out) && !out.empty()) return true;
    ::Sleep(30);
    return EnumEndpointsOnce(flow, out);
}

inline bool FindVirtualEndpoint(std::wstring& id, std::wstring& friendly)
{
    id.clear();
    friendly.clear();

    auto e = MakeEnumerator();
    if (!e) return false;

    Ptr<IMMDeviceCollection> coll;
    if (FAILED(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, coll.put())) || !coll) return false;

    UINT count = 0;
    if (FAILED(coll->GetCount(&count))) return false;

    for (UINT i = 0; i < count; ++i) {
        Ptr<IMMDevice> dev;
        if (FAILED(coll->Item(i, dev.put())) || !dev) continue;
        Endpoint ep;
        if (!ReadEndpoint(dev.get(), eRender, ep)) continue;
        if (!IsOurVirtualDevice(ep)) continue;
        id = ep.id;
        friendly = ep.friendly;
        return true;
    }
    return false;
}

inline bool EndpointVolume(const std::wstring& devId, Ptr<IAudioEndpointVolume>& out)
{
    auto e = MakeEnumerator();
    auto d = FindDevice(e.get(), devId);
    if (!d) return false;
    return SUCCEEDED(d->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, out.putv()));
}

inline bool GetEndpointState(const std::wstring& devId, float& volume, bool& mute)
{
    Ptr<IAudioEndpointVolume> v;
    if (!EndpointVolume(devId, v)) return false;
    if (FAILED(v->GetMasterVolumeLevelScalar(&volume))) return false;
    BOOL m = FALSE;
    if (FAILED(v->GetMute(&m))) return false;
    mute = (m != FALSE);
    return true;
}

inline bool GetEndpointPeak(const std::wstring& devId, float& left, float& right)
{
    left = 0.0f;
    right = 0.0f;
    Ptr<IAudioMeterInformation> meter;
    auto e = MakeEnumerator();
    auto d = FindDevice(e.get(), devId);
    if (!d || FAILED(d->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, meter.putv())))
        return false;
    UINT channels = 0;
    if (FAILED(meter->GetMeteringChannelCount(&channels)) || channels == 0) return false;
    float values[2] = {};
    if (channels <= 2)
    {
        if (FAILED(meter->GetChannelsPeakValues(channels, values))) return false;
        left = values[0];
        right = channels > 1 ? values[1] : values[0];
        return true;
    }
    std::vector<float> all(channels);
    if (FAILED(meter->GetChannelsPeakValues(channels, all.data()))) return false;
    left = all[0];
    right = all[1];
    return true;
}

inline bool SetEndpointVolume(const std::wstring& devId, float volume)
{
    Ptr<IAudioEndpointVolume> v;
    if (!EndpointVolume(devId, v)) return false;
    return SUCCEEDED(v->SetMasterVolumeLevelScalar(volume, nullptr));
}

inline bool SetEndpointMute(const std::wstring& devId, bool mute)
{
    Ptr<IAudioEndpointVolume> v;
    if (!EndpointVolume(devId, v)) return false;
    return SUCCEEDED(v->SetMute(mute ? TRUE : FALSE, nullptr));
}

class EndpointVolumeMonitor final
{
    class Callback final : public IAudioEndpointVolumeCallback
    {
    public:
        explicit Callback(std::function<void(float, bool)> handler)
            : handler_(std::move(handler)) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
        {
            if (!object) return E_POINTER;
            *object = nullptr;
            if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback))
            {
                *object = static_cast<IAudioEndpointVolumeCallback*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(::InterlockedIncrement(&references_));
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG references = static_cast<ULONG>(::InterlockedDecrement(&references_));
            if (references == 0) delete this;
            return references;
        }

        HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override
        {
            if (data && handler_) handler_(data->fMasterVolume, data->bMuted != FALSE);
            return S_OK;
        }

    private:
        LONG references_ = 1;
        std::function<void(float, bool)> handler_;
    };

public:
    ~EndpointVolumeMonitor()
    {
        if (volume_ && callback_)
            volume_->UnregisterControlChangeNotify(callback_);
        if (callback_) callback_->Release();
    }

    EndpointVolumeMonitor(const EndpointVolumeMonitor&) = delete;
    EndpointVolumeMonitor& operator=(const EndpointVolumeMonitor&) = delete;

    static std::shared_ptr<EndpointVolumeMonitor> Create(
        const std::wstring& deviceId, std::function<void(float, bool)> handler)
    {
        Ptr<IAudioEndpointVolume> volume;
        if (!EndpointVolume(deviceId, volume) || !volume) return {};

        auto monitor = std::shared_ptr<EndpointVolumeMonitor>(
            new EndpointVolumeMonitor(std::move(volume), std::move(handler)));
        if (FAILED(monitor->volume_->RegisterControlChangeNotify(monitor->callback_)))
            return {};
        return monitor;
    }

private:
    EndpointVolumeMonitor(Ptr<IAudioEndpointVolume>&& volume,
                          std::function<void(float, bool)> handler)
        : volume_(std::move(volume)), callback_(new Callback(std::move(handler))) {}

    Ptr<IAudioEndpointVolume> volume_;
    Callback* callback_ = nullptr;
};

inline std::wstring ProcessImagePath(DWORD pid)
{
    std::wstring path;
    if (pid == 0) return path;
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return path;
    wchar_t buf[1024] = {};
    DWORD n = ARRAYSIZE(buf);
    if (::QueryFullProcessImageNameW(h, 0, buf, &n) && n > 0) path.assign(buf, n);
    ::CloseHandle(h);
    return path;
}

inline std::wstring FileDescriptionOf(const std::wstring& path)
{
    std::wstring result;
    if (path.empty()) return result;
    DWORD dummy = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (!size) return result;
    std::vector<BYTE> buf(size);
    if (!::GetFileVersionInfoW(path.c_str(), 0, size, buf.data())) return result;

    struct LangCp { WORD lang; WORD cp; };
    LangCp* tr = nullptr;
    UINT trLen = 0;
    if (!::VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&tr, &trLen)) return result;
    if (!tr || trLen < sizeof(LangCp)) return result;

    wchar_t sub[128] = {};
    ::swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr[0].lang, tr[0].cp);
    LPWSTR val = nullptr;
    UINT valLen = 0;
    if (::VerQueryValueW(buf.data(), sub, (LPVOID*)&val, &valLen) && val && valLen > 0) result = val;
    return result;
}

inline std::wstring SessionDisplayName(DWORD pid, bool systemSounds)
{
    if (systemSounds) return text::Embedded().Resolve(L"audio.session.system_sounds");
    const std::wstring path = ProcessImagePath(pid);
    if (path.empty()) return text::Embedded().Resolve(L"audio.session.unknown_app");
    const std::wstring desc = FileDescriptionOf(path);
    if (!desc.empty()) return desc;
    size_t slash = path.find_last_of(L"\\/");
    std::wstring stem = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    if (stem.size() > 4 && _wcsicmp(stem.c_str() + stem.size() - 4, L".exe") == 0) stem.resize(stem.size() - 4);
    return stem;
}

inline std::wstring SessionGroupKey(DWORD pid, bool systemSounds)
{
    if (systemSounds) return L"#system";
    std::wstring path = ProcessImagePath(pid);
    std::transform(path.begin(), path.end(), path.begin(), towlower);
    return path.empty() ? (L"#pid" + std::to_wstring(pid)) : path;
}

inline Ptr<IAudioSessionManager2> SessionManager(IMMDevice* dev)
{
    Ptr<IAudioSessionManager2> m;
    if (!dev) return m;
    dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, m.putv());
    return m;
}

inline Ptr<ISimpleAudioVolume> ResolveSessionVolume(const std::wstring& devId, const std::wstring& sessionId)
{
    auto e = MakeEnumerator();
    auto dev = FindDevice(e.get(), devId);
    auto mgr = SessionManager(dev.get());
    if (!mgr) return {};

    Ptr<IAudioSessionEnumerator> list;
    if (FAILED(mgr->GetSessionEnumerator(list.put())) || !list) return {};
    int n = 0;
    if (FAILED(list->GetCount(&n))) return {};

    for (int i = 0; i < n; ++i) {
        Ptr<IAudioSessionControl> sc;
        if (FAILED(list->GetSession(i, sc.put())) || !sc) continue;
        Ptr<IAudioSessionControl2> c2;
        if (FAILED(sc->QueryInterface(__uuidof(IAudioSessionControl2), c2.putv())) || !c2) continue;
        LPWSTR got = nullptr;
        if (FAILED(c2->GetSessionInstanceIdentifier(&got))) continue;
        const std::wstring gotId = TakeDeviceId(got);
        if (!sessionId.empty() && gotId != sessionId) continue;
        Ptr<ISimpleAudioVolume> v;
        if (FAILED(sc->QueryInterface(__uuidof(ISimpleAudioVolume), v.putv()))) return {};
        return v;
    }
    return {};
}

inline Ptr<IAudioMeterInformation> ResolveSessionMeter(const std::wstring& devId, const std::wstring& sessionId)
{
    auto e = MakeEnumerator();
    auto dev = FindDevice(e.get(), devId);
    auto mgr = SessionManager(dev.get());
    if (!mgr) return {};

    Ptr<IAudioSessionEnumerator> list;
    if (FAILED(mgr->GetSessionEnumerator(list.put())) || !list) return {};
    int n = 0;
    if (FAILED(list->GetCount(&n))) return {};

    for (int i = 0; i < n; ++i)
    {
        Ptr<IAudioSessionControl> sc;
        if (FAILED(list->GetSession(i, sc.put())) || !sc) continue;
        Ptr<IAudioSessionControl2> c2;
        if (FAILED(sc->QueryInterface(__uuidof(IAudioSessionControl2), c2.putv())) || !c2) continue;
        LPWSTR got = nullptr;
        if (FAILED(c2->GetSessionInstanceIdentifier(&got))) continue;
        if (TakeDeviceId(got) != sessionId) continue;

        Ptr<IAudioMeterInformation> meter;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(IAudioMeterInformation), meter.putv())))
        {
            return meter;
        }
    }
    return {};
}

inline bool EnumSessionsOnce(const std::wstring& devId, std::vector<Session>& out)
{
    out.clear();
    auto e = MakeEnumerator();
    auto dev = FindDevice(e.get(), devId);
    auto mgr = SessionManager(dev.get());
    if (!mgr) return false;

    Ptr<IAudioSessionEnumerator> list;
    if (FAILED(mgr->GetSessionEnumerator(list.put())) || !list) return false;
    int n = 0;
    if (FAILED(list->GetCount(&n))) return false;

    for (int i = 0; i < n; ++i) {
        Ptr<IAudioSessionControl> sc;
        if (FAILED(list->GetSession(i, sc.put())) || !sc) continue;
        Ptr<IAudioSessionControl2> c2;
        if (FAILED(sc->QueryInterface(__uuidof(IAudioSessionControl2), c2.putv())) || !c2) continue;

        Session s;
        LPWSTR sid = nullptr;
        if (SUCCEEDED(c2->GetSessionInstanceIdentifier(&sid))) s.id = TakeDeviceId(sid);
        if (s.id.empty()) continue;

        s.systemSounds = (c2->IsSystemSoundsSession() == S_OK);
        if (!s.systemSounds) {
            DWORD pid = 0;
            if (SUCCEEDED(c2->GetProcessId(&pid))) s.pid = pid;
        }

        AudioSessionState state = AudioSessionStateInactive;
        if (SUCCEEDED(c2->GetState(&state))) s.state = state;

        Ptr<ISimpleAudioVolume> vol;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(ISimpleAudioVolume), vol.putv())) && vol) {
            vol->GetMasterVolume(&s.volume);
            BOOL m = FALSE;
            if (SUCCEEDED(vol->GetMute(&m))) s.mute = (m != FALSE);
            s.vol = std::move(vol);
        }

        s.name = SessionDisplayName(s.pid, s.systemSounds);
        s.key = SessionGroupKey(s.pid, s.systemSounds);
        out.push_back(std::move(s));
    }
    return true;
}

inline bool EnumSessions(const std::wstring& devId, std::vector<Session>& out)
{
    if (EnumSessionsOnce(devId, out) && !out.empty()) return true;
    ::Sleep(30);
    return EnumSessionsOnce(devId, out);
}

inline int AppSessionScore(AudioSessionState state, bool onPreferred, bool onDefault, bool sameInstance)
{
    int score = 0;
    if (onPreferred && state != AudioSessionStateExpired) score += 8;
    if (state == AudioSessionStateActive) score += 4;
    else if (state == AudioSessionStateExpired) score -= 4;
    if (onDefault) score += 2;
    if (sameInstance) score += 1;
    return score;
}

struct SessionTarget
{
    std::wstring deviceId;
    std::wstring instanceId;
    Ptr<ISimpleAudioVolume> volume;
    Ptr<IAudioMeterInformation> meter;
};

inline bool ResolveAppSessionTarget(const std::wstring& sessionId, DWORD processId, bool systemSounds,
                                    const std::wstring& preferredDeviceId, SessionTarget& out)
{
    out = SessionTarget{};
    if (sessionId.empty() && processId == 0 && !systemSounds) return false;

    std::vector<Endpoint> endpoints;
    if (!EnumEndpoints(eRender, endpoints)) return false;

    std::wstring defaultId;
    GetDefaultEndpointId(eRender, defaultId);

    int best = std::numeric_limits<int>::min();
    std::wstring bestDevice;
    std::wstring bestSession;

    for (const auto& endpoint : endpoints)
    {
        std::vector<Session> sessions;
        if (!EnumSessionsOnce(endpoint.id, sessions)) continue;

        for (const auto& session : sessions)
        {
            if (!session.vol) continue;

            const bool sameInstance = !sessionId.empty() && session.id == sessionId;
            const bool sameApp = systemSounds ? session.systemSounds : (processId != 0 && session.pid == processId);
            if (!sameInstance && !sameApp) continue;

            const int score = AppSessionScore(session.state, endpoint.id == preferredDeviceId,
                                              endpoint.id == defaultId, sameInstance);
            if (score <= best) continue;

            best = score;
            bestDevice = endpoint.id;
            bestSession = session.id;
        }
    }

    if (best == std::numeric_limits<int>::min()) return false;

    auto volume = ResolveSessionVolume(bestDevice, bestSession);
    if (!volume) return false;

    out.deviceId = std::move(bestDevice);
    out.instanceId = std::move(bestSession);
    out.volume = std::move(volume);
    out.meter = ResolveSessionMeter(out.deviceId, out.instanceId);
    return true;
}

inline bool SetSessionVolume(Session& s, const std::wstring& devId, float v)
{
    if (s.vol && SUCCEEDED(s.vol->SetMasterVolume(v, nullptr))) return true;
    s.vol = ResolveSessionVolume(devId, s.id);
    return s.vol && SUCCEEDED(s.vol->SetMasterVolume(v, nullptr));
}

inline bool SetSessionMute(Session& s, const std::wstring& devId, bool mute)
{
    const BOOL want = mute ? TRUE : FALSE;
    if (s.vol && SUCCEEDED(s.vol->SetMute(want, nullptr))) return true;
    s.vol = ResolveSessionVolume(devId, s.id);
    return s.vol && SUCCEEDED(s.vol->SetMute(want, nullptr));
}

inline Ptr<IAudioPolicyConfigFactory21H2> MakePolicyFactory()
{
    using RoGetActivationFactoryFn = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
    static RoGetActivationFactoryFn fn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (HMODULE combase = ::GetModuleHandleW(L"combase.dll")) {
            fn = reinterpret_cast<RoGetActivationFactoryFn>(::GetProcAddress(combase, "RoGetActivationFactory"));
        }
    }
    if (!fn) {
        log::Logger::Instance().WriteKey(log::Level::Debug, L"audio", L"log.interop.rogetactivationfactory_unavailable");
        return {};
    }

    const wchar_t* name = L"Windows.Media.Internal.AudioPolicyConfig";
    HSTRING hs = nullptr;
    if (FAILED(::WindowsCreateString(name, (UINT32)wcslen(name), &hs))) return {};

    Ptr<IAudioPolicyConfigFactory21H2> f;
    HRESULT hr = fn(hs, __uuidof(IAudioPolicyConfigFactory21H2), f.putv());
    if (FAILED(hr)) hr = fn(hs, __uuidof(IAudioPolicyConfigFactoryDownlevel), f.putv());
    ::WindowsDeleteString(hs);

    if (FAILED(hr)) {
        log::Logger::Instance().WriteKey(log::Level::Debug, L"audio", L"log.interop.policy_factory_activation_failed");
        return {};
    }
    return f;
}

inline std::wstring PackDeviceId(const std::wstring& rawId, EDataFlow flow)
{
    return (flow == eRender)
        ? (L"\\\\?\\SWD#MMDEVAPI#" + rawId + L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}")
        : (L"\\\\?\\SWD#MMDEVAPI#" + rawId + L"#{2eef81be-33fa-4800-9670-1cd474972c3f}");
}

inline std::wstring UnpackDeviceId(const std::wstring& packed)
{
    static const wchar_t kPrefix[] = L"\\\\?\\SWD#MMDEVAPI#";
    std::wstring r = packed;
    if (r.rfind(kPrefix, 0) == 0) r.erase(0, ARRAYSIZE(kPrefix) - 1);
    const size_t hash = r.rfind(L"#{");
    if (hash != std::wstring::npos) r.erase(hash);
    return r;
}

inline bool SetPersistedEndpointForRole(EDataFlow flow, DWORD pid, ERole role, const std::wstring& rawDeviceId)
{
    auto f = MakePolicyFactory();
    if (!f) return false;

    HSTRING hs = nullptr;
    if (!rawDeviceId.empty()) {
        const std::wstring packed = PackDeviceId(rawDeviceId, flow);
        if (FAILED(::WindowsCreateString(packed.c_str(), (UINT32)packed.size(), &hs))) return false;
    }
    const HRESULT hr = f->SetPersistedDefaultAudioEndpoint(pid, flow, role, hs);
    if (hs) ::WindowsDeleteString(hs);
    return SUCCEEDED(hr);
}

inline bool SetPersistedEndpoint(EDataFlow flow, DWORD pid, const std::wstring& rawDeviceId)
{
    return SetPersistedEndpointForRole(flow, pid, eConsole, rawDeviceId) &&
           SetPersistedEndpointForRole(flow, pid, eMultimedia, rawDeviceId);
}

inline bool GetPersistedEndpointForRole(EDataFlow flow, DWORD pid, ERole role, std::wstring& rawOut)
{
    rawOut.clear();
    auto f = MakePolicyFactory();
    if (!f) return false;
    HSTRING hs = nullptr;
    if (FAILED(f->GetPersistedDefaultAudioEndpoint(pid, flow, role, &hs))) return false;
    if (!hs) return true;
    UINT32 len = 0;
    const wchar_t* p = ::WindowsGetStringRawBuffer(hs, &len);
    const std::wstring packed(p ? p : L"", p ? len : 0);
    ::WindowsDeleteString(hs);
    rawOut = UnpackDeviceId(packed);
    return true;
}

inline bool GetPersistedEndpoint(EDataFlow flow, DWORD pid, std::wstring& rawOut)
{
    return GetPersistedEndpointForRole(flow, pid, eMultimedia, rawOut);
}

inline bool ClearAllAppRedirects()
{
    auto f = MakePolicyFactory();
    if (!f) return false;
    return SUCCEEDED(f->ClearAllPersistedApplicationDefaultEndpoints());
}

inline const IID kIidPolicyConfig =
    { 0xF8679F50, 0x850A, 0x41CF, { 0x9C, 0x72, 0x43, 0x0F, 0x29, 0x02, 0x90, 0xC8 } };
inline const IID kIidPolicyConfigVista =
    { 0x568B9108, 0x44BF, 0x40B4, { 0x90, 0x06, 0x86, 0xAF, 0xE5, 0xB5, 0xA6, 0x20 } };

inline Ptr<IPolicyConfig> CreatePolicyConfig()
{
    Ptr<::IUnknown> unknown;
    const HRESULT created = ::CoCreateInstance(__uuidof(PolicyConfigClient), nullptr, CLSCTX_ALL,
                                               __uuidof(::IUnknown), unknown.putv());
    if (FAILED(created) || !unknown) {
        log::Logger::Instance().WriteKey(log::Level::Debug, L"audio", L"log.interop.ipolicyconfig_unavailable");
        return {};
    }

    for (const IID& iid : { kIidPolicyConfig, kIidPolicyConfigVista }) {
        void* raw = nullptr;
        if (SUCCEEDED(unknown->QueryInterface(iid, &raw)) && raw != nullptr) {
            Ptr<IPolicyConfig> pc;
            pc.attach(reinterpret_cast<IPolicyConfig*>(raw));
            return pc;
        }
    }

    log::Logger::Instance().WriteKey(log::Level::Debug, L"audio", L"log.interop.ipolicyconfig_unavailable");
    return {};
}

inline HRESULT SetSystemDefaultEndpointForRoleResult(const std::wstring& rawDeviceId, ERole role)
{
    if (rawDeviceId.empty()) return E_INVALIDARG;

    auto pc = CreatePolicyConfig();
    if (!pc) return E_NOINTERFACE;

    return pc->SetDefaultEndpoint(rawDeviceId.c_str(), role);
}

inline bool CanSetSystemDefaultEndpoint()
{
    return static_cast<bool>(CreatePolicyConfig());
}

inline bool SetSystemDefaultEndpointForRole(const std::wstring& rawDeviceId, ERole role)
{
    const HRESULT hr = SetSystemDefaultEndpointForRoleResult(rawDeviceId, role);
    if (FAILED(hr)) {
        wchar_t hex[16] = {};
        ::swprintf_s(hex, L"%08lX", static_cast<unsigned long>(hr));
        log::Logger::Instance().WriteKeyFormat(log::Level::Debug, L"audio",
            L"log.interop.set_default_endpoint_failed_role",
            { std::to_wstring(static_cast<int>(role)), hex });
        return false;
    }
    return true;
}

inline bool SetSystemDefaultEndpoint(const std::wstring& rawDeviceId)
{
    return SetSystemDefaultEndpointForRole(rawDeviceId, eConsole) &&
           SetSystemDefaultEndpointForRole(rawDeviceId, eMultimedia);
}

}
