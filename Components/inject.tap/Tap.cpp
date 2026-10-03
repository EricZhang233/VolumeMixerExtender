#include "Tap.h"

#include "InjectionContract.h"
#include "Logger.h"
#include "Page.h"
#include "Platform.h"
#include "TextService.h"

#include "inspectable.h"
#include "ocidl.h"
#include "xamlOM.h"

#include <windows.h>

#include <atomic>
#include <array>
#include <functional>
#include <mutex>
#include <string>

void LogFooterAncestry(winrt::Windows::UI::Xaml::FrameworkElement const& footer);

namespace vmex::tap
{
    constexpr std::wstring_view kChannelName = L"tap";

    namespace
    {
        constexpr std::wstring_view kChannel = L"tap";

        std::mutex g_mutex;
        std::once_flag g_logging;
        HMODULE g_self = nullptr;
        std::filesystem::path g_configOverride;
        bool g_configured = false;
    }

    void SetSelfModule(HMODULE module)
    {
        g_self = module;
    }

    std::filesystem::path ModuleDirectory()
    {
        if (g_self == nullptr)
        {
            return platform::GetExecutableDirectory();
        }

        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const auto length = ::GetModuleFileNameW(g_self, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return platform::GetExecutableDirectory();
            }
            if (length < buffer.size())
            {
                buffer.resize(length);
                return std::filesystem::path(buffer).parent_path();
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    void AttachLoggingOnce()
    {
        std::call_once(g_logging, []()
        {
            text::AttachEmbeddedToLogger();
            log::Logger::Instance().AddSink(log::CreateFileSink(log::SessionLogFile(L"tap")));
            log::Logger::Instance().SetMinimumLevel(log::Level::Trace);
        });
    }

    std::filesystem::path ConfigModulePath()
    {
        {
            std::lock_guard<std::mutex> guard(g_mutex);
            if (!g_configOverride.empty())
            {
                return g_configOverride;
            }
        }
        return ModuleDirectory() / std::wstring(inject::kTapConfigFileName);
    }

    void ProvideInitData(const wchar_t* config)
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        g_configured = true;

        if (config == nullptr || *config == L'\0')
        {
            return;
        }

        const std::wstring_view text(config);
        const auto separator = text.find(L'=');
        if (separator != std::wstring_view::npos && text.substr(0, separator) == L"cfg")
        {
            g_configOverride = std::filesystem::path(text.substr(separator + 1));
            return;
        }
        g_configOverride = std::filesystem::path(text);
    }

    unsigned long ConfiguredSessionId()
    {
        wchar_t buffer[32] = {};
        const auto path = ConfigModulePath().wstring();
        const DWORD length = ::GetPrivateProfileStringW(
            std::wstring(inject::kTapConfigSection).c_str(),
            std::wstring(inject::kConfigKeySession).c_str(),
            L"", buffer, ARRAYSIZE(buffer), path.c_str());

        const unsigned long configured = static_cast<unsigned long>(std::wcstoul(buffer, nullptr, 10));
        if (length > 0 && configured > 0)
        {
            return configured;
        }

        unsigned long session = 0;
        if (::ProcessIdToSessionId(::GetCurrentProcessId(), &session) == FALSE || session == 0)
        {
            return 1;
        }
        return session;
    }

    void LogClassRequest(const CLSID& clsid, const IID& iid)
    {
        std::array<wchar_t, 48> clsidText{};
        std::array<wchar_t, 48> iidText{};
        ::StringFromGUID2(clsid, clsidText.data(), static_cast<int>(clsidText.size()));
        ::StringFromGUID2(iid, iidText.data(), static_cast<int>(iidText.size()));

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info, kChannel, L"log.tap.class_request",
            { L"clsid=" + std::wstring(clsidText.data()), L"iid=" + std::wstring(iidText.data()) });
    }

    std::wstring Hex(std::uint32_t value)
    {
        wchar_t buffer[16] = {};
        swprintf_s(buffer, L"0x%08X", value);
        return buffer;
    }

    void LogKey(std::wstring_view key, std::vector<std::wstring> arguments = {})
    {
        log::Logger::Instance().WriteKeyFormat(log::Level::Info, kChannel, key, arguments);
    }

    void LogGuid(std::wstring_view key, const GUID& guid)
    {
        std::array<wchar_t, 48> text{};
        ::StringFromGUID2(guid, text.data(), static_cast<int>(text.size()));
        LogKey(key, { std::wstring(text.data()) });
    }

    class Tap final : public IVisualTreeServiceCallback, public IObjectWithSite
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            if (ppv == nullptr) return E_POINTER;
            *ppv = nullptr;

            if (riid == __uuidof(IUnknown) || riid == __uuidof(IVisualTreeServiceCallback))
            {
                *ppv = static_cast<IVisualTreeServiceCallback*>(this);
                AddRef();
                LogGuid(L"log.tap.guid.qi_ok", riid);
                return S_OK;
            }

            if (riid == __uuidof(IObjectWithSite))
            {
                *ppv = static_cast<IObjectWithSite*>(this);
                AddRef();
                LogGuid(L"log.tap.guid.qi_ok", riid);
                return S_OK;
            }

            LogGuid(L"log.tap.guid.qi_nointerface", riid);
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(::InterlockedIncrement(&m_ref));
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const long remaining = ::InterlockedDecrement(&m_ref);
            if (remaining == 0) delete this;
            return static_cast<ULONG>(remaining);
        }

        HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override
        {
            log::Logger::Instance().WriteKey(log::Level::Info, kChannel,
                site == nullptr ? L"log.tap.setsite_null" : L"log.tap.setsite_site");

            if (site == nullptr)
            {
                m_diag = nullptr;
                m_vts = nullptr;
                return S_OK;
            }

            site->QueryInterface(__uuidof(IXamlDiagnostics), m_diag.put_void());
            if (!m_diag)
            {
                log::Logger::Instance().WriteKey(log::Level::Warn, kChannel, L"log.tap.setsite_no_diagnostics");
                return E_NOINTERFACE;
            }

            m_vts = m_diag.try_as<IVisualTreeService>();
            if (!m_vts)
            {
                log::Logger::Instance().WriteKey(log::Level::Warn, kChannel, L"log.tap.setsite_no_visual_tree_service");
                return E_NOINTERFACE;
            }

            const HRESULT hr = m_vts->AdviseVisualTreeChange(this);
            log::Logger::Instance().WriteKeyFormat(log::Level::Info, kChannel,
                L"log.tap.advise_visual_tree_change", { L"hr=" + std::to_wstring(static_cast<long>(hr)) });
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppvSite) override
        {
            if (ppvSite == nullptr) return E_POINTER;
            *ppvSite = nullptr;
            if (!m_diag) return E_FAIL;
            return m_diag->QueryInterface(riid, ppvSite);
        }

        HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation,
                                                     VisualElement element,
                                                     VisualMutationType mutationType) override
        {
            (void)relation;

            const wchar_t* name = element.Name ? element.Name : L"";

            if (mutationType != Add || name[0] == L'\0')
            {
                return S_OK;
            }

            if (::wcscmp(name, L"VolumeL2Button") == 0)
            {
                HookSoundPageEntry(element.Handle);
                return S_OK;
            }

            if (::wcscmp(name, L"Footer") != 0)
            {
                return S_OK;
            }

            if (IsAttached(m_footer))
            {
                log::Logger::Instance().WriteKey(log::Level::Debug, kChannel, L"log.tap.footer_again");
                return S_OK;
            }

            log::Logger::Instance().WriteKey(log::Level::Info, kChannel, L"log.tap.footer_appeared");
            TakeOverOnUiThread(element.Handle);
            return S_OK;
        }

    private:
        void HookSoundPageEntry(InstanceHandle handle)
        {
            IInspectable* raw = nullptr;
            const HRESULT hr = m_diag->GetIInspectableFromHandle(handle, &raw);
            if (FAILED(hr) || raw == nullptr) return;

            winrt::Windows::Foundation::IInspectable inspectable{ raw, winrt::take_ownership_from_abi };
            auto button = inspectable.try_as<winrt::Windows::UI::Xaml::Controls::Button>();
            if (!button) return;

            log::Logger::Instance().WriteKey(log::Level::Info, kChannel, L"log.tap.entry_hooked");

            button.Click([](winrt::Windows::Foundation::IInspectable const&,
                            winrt::Windows::UI::Xaml::RoutedEventArgs const&)
            {
                page::ExpectSoundPage();
            });
        }

        static bool IsAttached(winrt::Windows::UI::Xaml::FrameworkElement const& element)
        {
            if (!element) return false;
            try
            {
                return winrt::Windows::UI::Xaml::Media::VisualTreeHelper::GetParent(element) != nullptr;
            }
            catch (...)
            {
                return false;
            }
        }

        void TakeOverOnUiThread(InstanceHandle handle)        {
            IInspectable* raw = nullptr;
            const HRESULT hr = m_diag->GetIInspectableFromHandle(handle, &raw);
            if (FAILED(hr) || raw == nullptr)
            {
                log::Logger::Instance().WriteKey(log::Level::Warn, kChannel, L"log.tap.inspectable_failed");
                return;
            }

            winrt::Windows::Foundation::IInspectable inspectable{ raw, winrt::take_ownership_from_abi };
            auto footer = inspectable.try_as<winrt::Windows::UI::Xaml::FrameworkElement>();
            if (!footer)
            {
                log::Logger::Instance().WriteKey(log::Level::Warn, kChannel, L"log.tap.not_framework_element");
                return;
            }

            try { LogFooterAncestry(footer); } catch (...) {}

            bool takenOver = false;
            try
            {
                takenOver = page::TakeOver(footer);
            }
            catch (winrt::hresult_error const& error)
            {
                log::Logger::Instance().WriteKeyFormat(log::Level::Error, kChannel,
                    L"log.tap.takeover_threw_hr", { L"hr=" + std::to_wstring(static_cast<long>(error.code().value)) });
            }
            catch (...)
            {
                log::Logger::Instance().WriteKey(log::Level::Error, kChannel, L"log.tap.takeover_threw");
            }

            if (takenOver)
            {
                m_footer = footer;
            }
        }

        long m_ref = 1;
        winrt::com_ptr<IXamlDiagnostics> m_diag;
        winrt::com_ptr<IVisualTreeService> m_vts;
        winrt::Windows::UI::Xaml::FrameworkElement m_footer{nullptr};
    };

    class TapFactory final : public IClassFactory
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            if (ppv == nullptr) return E_POINTER;
            *ppv = nullptr;

            if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory))
            {
                *ppv = static_cast<IClassFactory*>(this);
                AddRef();
                LogGuid(L"log.tap.guid.factory_ok", riid);
                return S_OK;
            }
            LogGuid(L"log.tap.guid.factory_nointerface", riid);
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(::InterlockedIncrement(&m_ref));
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const long remaining = ::InterlockedDecrement(&m_ref);
            if (remaining == 0) delete this;
            return static_cast<ULONG>(remaining);
        }

        HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
        {
            LogGuid(L"log.tap.guid.create_instance_riid", riid);

            if (ppv == nullptr) return E_POINTER;
            *ppv = nullptr;
            if (outer != nullptr) return CLASS_E_NOAGGREGATION;

            auto* tap = new (std::nothrow) Tap();
            if (tap == nullptr) return E_OUTOFMEMORY;

            const HRESULT hr = tap->QueryInterface(riid, ppv);
            tap->Release();
            LogKey(L"log.tap.create_instance", { L"hr=" + Hex(static_cast<std::uint32_t>(hr)) });
            return hr;
        }

        HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }

    private:
        long m_ref = 1;
    };

    IClassFactory* CreateFactory()
    {
        return new (std::nothrow) TapFactory();
    }
}

std::wstring TapPageDescribeNode(const winrt::Windows::UI::Xaml::DependencyObject& node)
{
    using namespace winrt::Windows::UI::Xaml;

    if (!node) return L"<null>";

    std::wstring text;
    try { text = winrt::get_class_name(node); } catch (...) { text = L"<unnamed>"; }

    if (auto element = node.try_as<FrameworkElement>())
    {
        try
        {
            const auto name = element.Name();
            if (!name.empty()) text += L" name=[" + std::wstring(name) + L"]";
        }
        catch (...) {}

        try
        {
            const auto id = winrt::Windows::UI::Xaml::Automation::AutomationProperties::GetAutomationId(element);
            if (!id.empty()) text += L" automationId=[" + std::wstring(id) + L"]";
        }
        catch (...) {}

        try { text += L" vis=" + std::to_wstring(static_cast<int>(element.Visibility())); } catch (...) {}
        try { text += L" loaded=" + std::to_wstring(element.IsLoaded() ? 1 : 0); } catch (...) {}
        try
        {
            text += L" size=" + std::to_wstring(static_cast<long>(element.ActualWidth())) + L"x" +
                    std::to_wstring(static_cast<long>(element.ActualHeight()));
        }
        catch (...) {}
    }

    if (auto items = node.try_as<winrt::Windows::UI::Xaml::Controls::ItemsControl>())
    {
        try { text += L" items=" + std::to_wstring(items.Items().Size()); } catch (...) {}
    }

    UINT32 count = 0;
    try { count = winrt::Windows::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(node); } catch (...) {}
    text += L" children=" + std::to_wstring(count);
    return text;
}

void LogFooterAncestry(winrt::Windows::UI::Xaml::FrameworkElement const& footer)
{
    using namespace winrt::Windows::UI::Xaml;
    using namespace winrt::Windows::UI::Xaml::Media;

    vmex::tap::LogKey(L"log.tap.footer_node", { TapPageDescribeNode(footer) });

    DependencyObject current = footer;
    for (int i = 0; i < 16; ++i)
    {
        DependencyObject parent{nullptr};
        try { parent = VisualTreeHelper::GetParent(current); } catch (...) { break; }
        if (!parent)
        {
            vmex::tap::LogKey(L"log.tap.ancestry_no_parent", { std::to_wstring(i) });
            break;
        }

        vmex::tap::LogKey(L"log.tap.ancestry_up", { std::to_wstring(i), TapPageDescribeNode(parent) });
        current = parent;
    }
}

void TapPageSendPipe(const char* line)
{
    if (line == nullptr) return;

    const std::wstring pipe = vmex::inject::TapPipeName(vmex::tap::ConfiguredSessionId());
    HANDLE handle = ::CreateFileW(pipe.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        vmex::log::Logger::Instance().WriteKey(vmex::log::Level::Warn, L"tap", L"log.tap.pipe_unavailable");
        return;
    }

    DWORD written = 0;
    ::WriteFile(handle, line, static_cast<DWORD>(::strlen(line)), &written, nullptr);
    ::CloseHandle(handle);
}

namespace
{
    constexpr double kFooterInset = 4.0;

    constexpr std::array<winrt::Windows::UI::Xaml::HorizontalAlignment, 3> kSlotAlignments{
        winrt::Windows::UI::Xaml::HorizontalAlignment::Left,
        winrt::Windows::UI::Xaml::HorizontalAlignment::Center,
        winrt::Windows::UI::Xaml::HorizontalAlignment::Right,
    };

    template <typename T>
    T FindDescendant(winrt::Windows::UI::Xaml::DependencyObject const& node, int depth)
    {
        using namespace winrt::Windows::UI::Xaml;
        using namespace winrt::Windows::UI::Xaml::Media;

        if (!node || depth > 20) return nullptr;

        UINT32 count = 0;
        try { count = VisualTreeHelper::GetChildrenCount(node); } catch (...) { return nullptr; }

        for (UINT32 i = 0; i < count; ++i)
        {
            DependencyObject child{nullptr};
            try { child = VisualTreeHelper::GetChild(node, i); } catch (...) { continue; }
            if (auto match = child.try_as<T>()) return match;
            if (auto found = FindDescendant<T>(child, depth + 1)) return found;
        }
        return nullptr;
    }
}

FooterMount TapPageMountFooterRow(const winrt::Windows::UI::Xaml::FrameworkElement& footer,
                                 const std::vector<winrt::Windows::UI::Xaml::FrameworkElement>& items)
{
    using namespace winrt::Windows::UI::Xaml;
    using namespace winrt::Windows::UI::Xaml::Controls;
    using namespace winrt::Windows::UI::Xaml::Media;

    FooterMount mount;

    FrameworkElement model = FindDescendant<Button>(footer, 0);
    if (!model) model = FindDescendant<HyperlinkButton>(footer, 0);
    if (!model)
    {
        mount.reason = "系统项（Button/HyperlinkButton）还没realize";
        return mount;
    }

    DependencyObject container = model;
    Panel panel{nullptr};
    for (int i = 0; i < 12 && container; ++i)
    {
        DependencyObject parent{nullptr};
        try { parent = VisualTreeHelper::GetParent(container); } catch (...) { break; }
        if (!parent) break;
        if (auto candidate = parent.try_as<Panel>()) { panel = candidate; break; }
        container = parent;
    }

    auto uiContainer = container ? container.try_as<FrameworkElement>() : nullptr;
    if (!panel || !uiContainer)
    {
        mount.reason = "系统项上面没有 Panel（ItemContainer 找不到）";
        return mount;
    }

    Grid row;

    double rowHeight = 0;
    try { rowHeight = panel.ActualHeight(); } catch (...) {}
    if (rowHeight > 0) row.MinHeight(rowHeight);

    double itemHeight = 0;
    try { itemHeight = model.Height(); } catch (...) {}

    UINT32 index = 0;
    if (!panel.Children().IndexOf(uiContainer, index))
    {
        mount.reason = "ItemContainer 不在它自己的 Panel 里";
        return mount;
    }

    panel.Children().RemoveAt(index);
    row.Children().Append(uiContainer);
    mount.systemItem = uiContainer;

    for (std::size_t i = 0; i < items.size() && i < kSlotAlignments.size(); ++i)
    {
        auto item = items[i];
        if (itemHeight > 0) item.Height(itemHeight);
        item.VerticalAlignment(VerticalAlignment::Center);
        item.HorizontalAlignment(kSlotAlignments[i]);

        const bool leading = (i == 0);
        const bool trailing = (i + 1 == items.size());
        item.Margin(ThicknessHelper::FromLengths(leading ? kFooterInset : 0.0, 0,
                                                 trailing ? kFooterInset : 0.0, 0));

        row.Children().Append(item);
    }

    panel.Children().InsertAt(index, row);
    mount.ok = true;
    return mount;
}

void TapPageDumpTree(const winrt::Windows::UI::Xaml::DependencyObject& root, int maxDepth)
{
    using namespace winrt::Windows::UI::Xaml;
    using namespace winrt::Windows::UI::Xaml::Media;

    std::function<void(DependencyObject const&, int)> walk =
        [&walk, maxDepth](DependencyObject const& node, int depth)
    {
        if (!node || depth > maxDepth) return;

        vmex::tap::LogKey(L"log.tap.tree_node",
            { L"  " + std::wstring(static_cast<std::size_t>(depth * 2), L' '), TapPageDescribeNode(node) });

        UINT32 count = 0;
        try { count = VisualTreeHelper::GetChildrenCount(node); } catch (...) {}

        for (UINT32 i = 0; i < count; ++i)
        {
            DependencyObject child{nullptr};
            try { child = VisualTreeHelper::GetChild(node, i); } catch (...) { continue; }
            walk(child, depth + 1);
        }
    };

    vmex::tap::LogKey(L"log.tap.tree_begin");
    walk(root, 0);
    vmex::tap::LogKey(L"log.tap.tree_end");
}

extern "C" __declspec(dllexport) void __stdcall VmExtTapProvideInitData(const wchar_t* config)
{
    vmex::tap::AttachLoggingOnce();
    vmex::tap::ProvideInitData(config);
}

extern "C" HRESULT __stdcall DllGetClassObject(const CLSID& clsid, const IID& iid, void** ppv)
{
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    vmex::tap::AttachLoggingOnce();
    vmex::tap::LogClassRequest(clsid, iid);

    CLSID expected{};
    const std::wstring id(vmex::inject::kTapClassId);
    if (FAILED(::CLSIDFromString(id.c_str(), &expected)) || clsid != expected)
    {
        vmex::log::Logger::Instance().WriteKeyFormat(vmex::log::Level::Warn, vmex::tap::kChannelName,
            L"log.tap.class_mismatch", { id });
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto* factory = vmex::tap::CreateFactory();
    if (factory == nullptr) return E_OUTOFMEMORY;

    const HRESULT hr = factory->QueryInterface(iid, ppv);
    factory->Release();
    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow()
{
    return S_FALSE;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        ::DisableThreadLibraryCalls(module);
        vmex::tap::SetSelfModule(module);
    }
    return TRUE;
}
