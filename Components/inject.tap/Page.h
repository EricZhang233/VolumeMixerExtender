#pragma once

#include "AutostartEntry.h"
#include "AppIcons.h"
#include "AudioDeviceManager.h"
#include "AudioInterop.h"
#include "InjectionContract.h"
#include "Logger.h"
#include "Platform.h"
#include "EndpointPolicyService.h"
#include "Strings.h"
#include "TextService.h"
#include "UserSettings.h"

#include <windows.h>
#include <shellapi.h>

#undef GetCurrentTime

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <cstdint>
#include <atomic>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

extern int TapPageSendPipe(const char* line);

struct FooterMount final
{
    winrt::Windows::UI::Xaml::UIElement systemItem{nullptr};
    bool ok = false;
    const char* reason = "";
};

extern FooterMount TapPageMountFooterRow(winrt::Windows::UI::Xaml::FrameworkElement const& footer,
                                        std::vector<winrt::Windows::UI::Xaml::FrameworkElement> const& items);

extern void TapPageDumpTree(winrt::Windows::UI::Xaml::DependencyObject const& root, int maxDepth);

extern std::wstring TapPageDescribeNode(winrt::Windows::UI::Xaml::DependencyObject const& node);

namespace vmex::tap::page
{
    namespace PGX   = winrt::Windows::UI::Xaml;
    namespace PGXC  = winrt::Windows::UI::Xaml::Controls;
    namespace PGXCP = winrt::Windows::UI::Xaml::Controls::Primitives;
    namespace PGXM  = winrt::Windows::UI::Xaml::Media;
    namespace PGXMI = winrt::Windows::UI::Xaml::Media::Imaging;
    namespace PGXSH = winrt::Windows::UI::Xaml::Shapes;
    namespace PGXAU = winrt::Windows::UI::Xaml::Automation;
    namespace PGUI  = winrt::Windows::UI;
    namespace PGF   = winrt::Windows::Foundation;
    namespace PGXIN = winrt::Windows::UI::Xaml::Input;

    inline void LogKey(std::wstring_view key, const std::vector<std::wstring>& args = {})
    {
        static std::once_flag attach;
        std::call_once(attach, []() { text::AttachEmbeddedToLogger(); });

        const std::wstring resolved = text::Embedded().ResolveFormat(key, args);
        log::Logger::Instance().Write(log::Level::Debug, L"page", resolved);
    }

    inline std::string Narrow(const std::wstring& value)
    {
        if (value.empty()) return std::string();
        const int n = ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                                            nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
        if (n > 0)
        {
            ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                                  out.data(), n, nullptr, nullptr);
        }
        return out;
    }

    struct State final
    {
        bool recordingMode = false;
        std::wstring savedDefaultRender;
        std::wstring listenEndpoint;
        bool showDriverName = true;
        int page = 0;
        bool inputExpanded = false;

        bool expectSoundPage = false;
        unsigned long long expectTick = 0;
    };

    [[nodiscard]] inline State& Settings()
    {
        static State state;
        return state;
    }

    struct PageData final
    {
        std::vector<audio::DeviceInfo> render;
        std::vector<audio::DeviceInfo> capture;
        std::vector<audio::SessionInfo> apps;
        std::vector<std::unique_ptr<audio::IAudioSessionHandle>> handles;
        std::wstring defaultRender;
        std::wstring defaultCapture;
        std::wstring appsEndpoint;
    };

    struct Context;
    using ContextPtr = std::shared_ptr<Context>;

    struct Context final
    {
        std::unique_ptr<audio::IAudioDeviceManager> manager;
        std::unique_ptr<audio::IEndpointPolicyService> endpointPolicy;
        PGX::FrameworkElement footer{nullptr};
        PGXC::ScrollViewer list{nullptr};
        PGX::FrameworkElement pageWindow{nullptr};
        PGF::IInspectable savedContent{nullptr};
        PGXC::Button slotLeft{nullptr};
        PGXC::Button slotMid{nullptr};
        PGXC::Button slotRight{nullptr};
        PGX::UIElement systemItem{nullptr};
        std::map<std::wstring, std::wstring> redirects;
        std::vector<std::shared_ptr<audio::detail::EndpointVolumeMonitor>> endpointMonitors;
        std::vector<PGX::DispatcherTimer> meterTimers;
        std::shared_ptr<PageData> data;

        bool mounting = false;
        bool mountDone = false;
        bool mountGaveUp = false;
        bool mountChainRunning = false;
        int mountAttempts = 0;
        unsigned long long layoutTicks = 0;
        unsigned long long startedTick = 0;
        std::wstring lastFooterState;

        bool committed = false;
        bool abandoned = false;
        unsigned long long lastIdentifyTick = 0;
    };

    inline PGXC::TextBlock Text(winrt::hstring const& value, double size)
    {
        PGXC::TextBlock block;
        block.Text(value);
        block.FontSize(size);
        block.TextTrimming(PGX::TextTrimming::CharacterEllipsis);
        return block;
    }

    inline constexpr double kBodyInset = 8.0;
    inline constexpr bool kRecordingModeEnabled = false;

    inline PGXC::TextBlock SectionLabel(winrt::hstring const& value)
    {
        PGXC::TextBlock block;
        block.Text(value);
        block.FontSize(12);
        block.Opacity(0.75);
        block.Margin(PGX::ThicknessHelper::FromLengths(0, 10, 0, 4));
        return block;
    }

    inline PGXC::Button BareButton(winrt::hstring const& value)
    {
        PGXC::Button button;
        button.Content(winrt::box_value(value));
        button.MinHeight(0);
        button.Background(nullptr);
        button.BorderThickness(PGX::ThicknessHelper::FromLengths(0, 0, 0, 0));
        return button;
    }

    inline PGXC::Button FooterButton(winrt::hstring const& value)
    {
        PGXC::Button button = BareButton(value);
        button.FontSize(12);
        button.Padding(PGX::ThicknessHelper::FromLengths(11, 0, 11, 0));
        return button;
    }

    inline PGXC::StackPanel NameBlock(std::wstring const& name, std::wstring const& subtitle)
    {
        PGXC::StackPanel panel;
        panel.VerticalAlignment(PGX::VerticalAlignment::Center);

        PGXC::TextBlock title = Text(winrt::hstring(name), 13);
        title.TextWrapping(PGX::TextWrapping::WrapWholeWords);
        title.MaxLines(2);
        panel.Children().Append(title);

        if (!subtitle.empty())
        {
            PGXC::TextBlock sub = Text(winrt::hstring(subtitle), 11);
            sub.Opacity(0.65);
            panel.Children().Append(sub);
        }
        return panel;
    }

    inline PGXC::Grid Row(double minHeight)
    {
        PGXC::Grid grid;
        grid.MinHeight(minHeight);
        grid.Padding(PGX::ThicknessHelper::FromLengths(0, 4, 0, 4));

        PGXC::ColumnDefinition leading;
        PGXC::ColumnDefinition flexible;
        PGXC::ColumnDefinition trailing;
        leading.Width(PGX::GridLengthHelper::Auto());
        flexible.Width(PGX::GridLengthHelper::FromValueAndType(1, PGX::GridUnitType::Star));
        trailing.Width(PGX::GridLengthHelper::Auto());
        grid.ColumnDefinitions().Append(leading);
        grid.ColumnDefinitions().Append(flexible);
        grid.ColumnDefinitions().Append(trailing);
        return grid;
    }

    inline PGXC::TextBlock Glyph(wchar_t const* code, double size)
    {
        PGXC::TextBlock block = Text(code, size);
        block.FontFamily(PGXM::FontFamily(L"Segoe MDL2 Assets"));
        block.VerticalAlignment(PGX::VerticalAlignment::Center);
        block.Margin(PGX::ThicknessHelper::FromLengths(0, 0, 10, 0));
        return block;
    }

    inline PGXM::SolidColorBrush DangerBrush(std::uint8_t alpha)
    {
        return PGXM::SolidColorBrush(PGUI::ColorHelper::FromArgb(alpha, 0xE5, 0x48, 0x4D));
    }

    [[nodiscard]] inline PGUI::Color AccentColor()
    {
        if (const auto application = PGX::Application::Current())
        {
            if (const auto resources = application.Resources())
            {
                if (resources.HasKey(winrt::box_value(L"SystemControlHighlightAccentBrush")))
                {
                    if (auto brush = resources.Lookup(winrt::box_value(L"SystemControlHighlightAccentBrush")).try_as<PGXM::Brush>())
                    {
                        if (auto solid = brush.try_as<PGXM::SolidColorBrush>())
                        {
                            return solid.Color();
                        }
                    }
                }
            }
        }

        return PGUI::ColorHelper::FromArgb(0xFF, 0x4C, 0xC2, 0xFF);
    }

    [[nodiscard]] inline PGXM::SolidColorBrush AccentBrush()
    {
        return PGXM::SolidColorBrush(AccentColor());
    }

    [[nodiscard]] inline PGXM::SolidColorBrush DimAccentBrush()
    {
        const auto color = AccentColor();
        return PGXM::SolidColorBrush(PGUI::ColorHelper::FromArgb(
            color.A, static_cast<std::uint8_t>(color.R * 0.58f),
            static_cast<std::uint8_t>(color.G * 0.58f),
            static_cast<std::uint8_t>(color.B * 0.58f)));
    }

    inline void ApplyDangerVisual(PGXC::Button const& button, bool armed)
    {
        const auto background = DangerBrush(armed ? 0x38 : 0x10);
        const auto border = DangerBrush(armed ? 0xE6 : 0x73);

        button.Background(background);
        button.BorderBrush(border);

        auto resources = button.Resources();
        resources.Insert(winrt::box_value(L"ButtonBackgroundPointerOver"), background);
        resources.Insert(winrt::box_value(L"ButtonBackgroundPressed"), border);
        resources.Insert(winrt::box_value(L"ButtonBorderBrushPointerOver"), border);
        resources.Insert(winrt::box_value(L"ButtonBorderBrushPressed"), border);
    }

    inline PGXC::Button DangerButton(winrt::hstring const& value)
    {
        PGXC::Button button;
        button.Content(winrt::box_value(value));
        button.FontSize(13);
        button.MinHeight(40);
        button.CornerRadius(PGX::CornerRadiusHelper::FromUniformRadius(5));
        button.Foreground(DangerBrush(0xFF));
        button.BorderThickness(PGX::ThicknessHelper::FromLengths(1, 1, 1, 1));
        auto resources = button.Resources();
        const auto text = DangerBrush(0xFF);
        resources.Insert(winrt::box_value(L"ButtonForegroundPointerOver"), text);
        resources.Insert(winrt::box_value(L"ButtonForegroundPressed"), text);

        ApplyDangerVisual(button, false);
        return button;
    }

    [[nodiscard]] inline winrt::hstring MuteGlyph(bool muted)
    {
        return muted ? winrt::hstring(L"\xE74F") : winrt::hstring(L"\xE767");
    }

    inline PGXC::Button MakeMuteButton(bool muted)
    {
        PGXC::TextBlock icon = Text(MuteGlyph(muted), 14);
        icon.FontFamily(PGXM::FontFamily(L"Segoe MDL2 Assets"));

        PGXC::Button button;
        button.Content(icon);
        button.MinHeight(0);
        button.Padding(PGX::ThicknessHelper::FromLengths(8, 4, 8, 4));
        button.Background(nullptr);
        button.BorderThickness(PGX::ThicknessHelper::FromLengths(0, 0, 0, 0));
        return button;
    }

    inline void SetMuteGlyph(PGXC::Button const& button, bool muted)
    {
        if (auto icon = button.Content().try_as<PGXC::TextBlock>())
        {
            icon.Text(MuteGlyph(muted));
        }
    }

    inline PGXC::Slider MakeVolumeSlider(float level)
    {
        PGXC::Slider slider;
        slider.Minimum(0);
        slider.Maximum(100);
        slider.Value(level * 100.0);
        slider.Width(150);
        slider.MinHeight(0);
        slider.VerticalAlignment(PGX::VerticalAlignment::Center);
        slider.Foreground(DimAccentBrush());
        return slider;
    }

    struct StereoMeter final
    {
        PGXC::Grid layer{nullptr};
        PGXSH::Rectangle left{nullptr};
        PGXSH::Rectangle right{nullptr};
    };

    inline StereoMeter MakeStereoMeter()
    {
        StereoMeter meter;
        meter.layer = PGXC::Grid();
        meter.layer.Width(150);
        meter.layer.Height(32);

        meter.left = PGXSH::Rectangle();
        meter.right = PGXSH::Rectangle();
        for (const auto& bar : { meter.left, meter.right })
        {
            bar.Width(0);
            bar.Height(2);
            bar.IsHitTestVisible(false);
            bar.HorizontalAlignment(PGX::HorizontalAlignment::Left);
            bar.VerticalAlignment(PGX::VerticalAlignment::Center);
        }
        const auto accent = AccentBrush();
        meter.left.Fill(accent);
        meter.right.Fill(accent);
        meter.left.Margin(PGX::ThicknessHelper::FromLengths(0, -2, 0, 0));
        meter.right.Margin(PGX::ThicknessHelper::FromLengths(0, 1, 0, 0));
        return meter;
    }

    inline void UpdateStereoMeter(StereoMeter const& meter, PGXC::Slider const& slider,
                                  float leftPeak, float rightPeak)
    {
        const double trackWidth = std::max(0.0, slider.ActualWidth() - 12.0);
        const double volume = std::clamp(slider.Value() / 100.0, 0.0, 1.0);
        meter.left.Width(trackWidth * std::clamp(static_cast<double>(leftPeak), 0.0, 1.0) * volume);
        meter.right.Width(trackWidth * std::clamp(static_cast<double>(rightPeak), 0.0, 1.0) * volume);
    }

    inline void SendSetDefault(bool render, const std::wstring& deviceId);
    inline void MountPage(ContextPtr const& context, int page);

    inline PGXC::Grid MakeEndpointRow(ContextPtr const& context, const audio::DeviceInfo& device)
    {
        float level = 0.0f;
        bool muted = false;
        audio::EndpointVolume volume;
        if (context->manager && context->manager->GetVolume(device.id, volume).IsOk())
        {
            level = volume.level;
            muted = volume.muted;
        }

        PGXC::Grid grid = Row(44);
        PGXC::TextBlock icon = Glyph(L"\xE7F5", 14);
        PGXC::Grid::SetColumn(icon, 0);
        grid.Children().Append(icon);

        std::wstring name = audio::DisplayDeviceName(device, Settings().showDriverName);
        PGXC::StackPanel block = NameBlock(name, std::wstring());
        PGXC::Grid::SetColumn(block, 1);
        grid.Children().Append(block);

        const std::wstring deviceId = device.id;
        PGXC::StackPanel trailing;
        trailing.Orientation(PGXC::Orientation::Horizontal);
        trailing.VerticalAlignment(PGX::VerticalAlignment::Center);

        PGXC::Button mute = MakeMuteButton(muted);
        ContextPtr weakContext = context;
        mute.Click([weakContext, deviceId, mute](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
        {
            audio::EndpointVolume current;
            if (!weakContext->manager || !weakContext->manager->GetVolume(deviceId, current).IsOk()) return;
            if (weakContext->manager->SetMuted(deviceId, !current.muted).IsOk())
            {
                SetMuteGlyph(mute, !current.muted);
            }
        });
        trailing.Children().Append(mute);

        PGXC::Slider slider = MakeVolumeSlider(level);
        auto dragging = std::make_shared<std::atomic_bool>(false);
        auto synchronizing = std::make_shared<std::atomic_bool>(false);
        slider.ValueChanged([weakContext, deviceId, synchronizing](PGF::IInspectable const& sender,
                                                                    PGXCP::RangeBaseValueChangedEventArgs const&)
        {
            if (synchronizing->load(std::memory_order_acquire)) return;
            if (!weakContext->manager) return;
            if (auto s = sender.try_as<PGXC::Slider>())
            {
                weakContext->manager->SetVolume(deviceId, static_cast<float>(s.Value() / 100.0));
            }
        });
        slider.PointerPressed([dragging](PGF::IInspectable const&, PGXIN::PointerRoutedEventArgs const&)
        {
            dragging->store(true, std::memory_order_release);
        });
        auto refreshAfterDrag = [weakContext, deviceId, slider, dragging, synchronizing]()
        {
            dragging->store(false, std::memory_order_release);
            audio::EndpointVolume current;
            if (!weakContext->manager || !weakContext->manager->GetVolume(deviceId, current).IsOk()) return;
            synchronizing->store(true, std::memory_order_release);
            slider.Value(current.level * 100.0);
            synchronizing->store(false, std::memory_order_release);
        };
        slider.PointerReleased([refreshAfterDrag](PGF::IInspectable const&, PGXIN::PointerRoutedEventArgs const&)
        {
            refreshAfterDrag();
        });
        slider.PointerCaptureLost([refreshAfterDrag](PGF::IInspectable const&, PGXIN::PointerRoutedEventArgs const&)
        {
            refreshAfterDrag();
        });
        StereoMeter meter = MakeStereoMeter();
        meter.layer.Children().Append(slider);
        meter.layer.Children().Append(meter.left);
        meter.layer.Children().Append(meter.right);
        trailing.Children().Append(meter.layer);

        PGX::DispatcherTimer meterTimer;
        meterTimer.Interval(std::chrono::milliseconds(50));
        meterTimer.Tick([weakContext, deviceId, meter, slider](PGF::IInspectable const&, PGF::IInspectable const&)
        {
            float left = 0.0f;
            float right = 0.0f;
            if (weakContext->manager && weakContext->manager->GetPeak(deviceId, left, right).IsOk())
            {
                UpdateStereoMeter(meter, slider, left, right);
            }
        });
        meterTimer.Start();
        context->meterTimers.push_back(meterTimer);

        auto dispatcher = slider.Dispatcher();
        if (dispatcher)
        {
            auto monitor = audio::detail::EndpointVolumeMonitor::Create(
                deviceId, [slider, mute, dispatcher, dragging, synchronizing](float externalLevel, bool externalMuted)
                {
                    try
                    {
                        dispatcher.RunAsync(
                            winrt::Windows::UI::Core::CoreDispatcherPriority::Low,
                            [slider, mute, dragging, synchronizing, externalLevel, externalMuted]()
                            {
                                if (dragging->load(std::memory_order_acquire)) return;
                                synchronizing->store(true, std::memory_order_release);
                                slider.Value(externalLevel * 100.0);
                                synchronizing->store(false, std::memory_order_release);
                                SetMuteGlyph(mute, externalMuted);
                            });
                    }
                    catch (const winrt::hresult_error&)
                    {
                        LogKey(L"log.page.dispatchFailed");
                    }
                });
            if (monitor) context->endpointMonitors.push_back(std::move(monitor));
        }

        PGXC::Grid::SetColumn(trailing, 2);
        grid.Children().Append(trailing);
        return grid;
    }

    inline void SendSetDefault(bool render, const std::wstring& deviceId);

    inline PGXC::ListView MakeEndpointList(ContextPtr const& context,
                                            const std::vector<audio::DeviceInfo>& devices,
                                            bool render)
    {
        PGXC::ListView list;
        list.SelectionMode(PGXC::ListViewSelectionMode::Single);
        list.IsItemClickEnabled(false);
        list.Padding(PGX::ThicknessHelper::FromLengths(0, 0, 0, 0));
        list.BorderThickness(PGX::ThicknessHelper::FromLengths(0, 0, 0, 0));
        list.Background(nullptr);

        for (const auto& device : devices)
        {
            list.Items().Append(MakeEndpointRow(context, device));
        }

        auto initializing = std::make_shared<bool>(true);
        const auto snapshot = devices;
        list.SelectionChanged([context, snapshot, render, initializing](PGF::IInspectable const& sender,
                                                                          PGXC::SelectionChangedEventArgs const&)
        {
            if (*initializing) return;
            auto self = sender.try_as<PGXC::ListView>();
            if (!self) return;
            const int index = self.SelectedIndex();
            if (index < 0 || static_cast<std::size_t>(index) >= snapshot.size()) return;

            const std::wstring deviceId = snapshot[static_cast<std::size_t>(index)].id;
            SendSetDefault(render, deviceId);
            auto& current = render ? context->data->render : context->data->capture;
            for (auto& item : current) item.isDefault = item.id == deviceId;
            if (render) context->data->defaultRender = deviceId;
            else context->data->defaultCapture = deviceId;
            MountPage(context, Settings().page);
        });

        for (std::size_t i = 0; i < devices.size(); ++i)
        {
            if (devices[i].isDefault)
            {
                list.SelectedIndex(static_cast<int>(i));
                break;
            }
        }
        *initializing = false;
        return list;
    }

    inline PGXC::StackPanel BuildVolumeLayer(ContextPtr const& context)
    {
        PGXC::StackPanel layer;
        layer.Children().Append(SectionLabel(winrt::hstring(text::Embedded().Resolve(L"page.section.volume"))));

        layer.Children().Append(MakeEndpointList(context, context->data->render, true));

        if (context->data->capture.empty()) return layer;

        PGXC::Grid head = Row(44);
        PGXC::TextBlock icon = Glyph(L"\xE720", 14);
        PGXC::Grid::SetColumn(icon, 0);
        head.Children().Append(icon);

        std::wstring caption = text::Embedded().ResolveFormat(
            L"page.capture.caption", { std::to_wstring(context->data->capture.size()) });
        PGXC::StackPanel block = NameBlock(caption, std::wstring());
        PGXC::Grid::SetColumn(block, 1);
        head.Children().Append(block);

        PGXC::TextBlock arrow = Glyph(Settings().inputExpanded ? L"\xE70E" : L"\xE70D", 12);
        arrow.Margin(PGX::ThicknessHelper::FromLengths(0, 0, 0, 0));
        PGXC::Grid::SetColumn(arrow, 2);
        head.Children().Append(arrow);

        PGXC::StackPanel body;
        body.Margin(PGX::ThicknessHelper::FromLengths(0, 0, 0, 0));
        body.Visibility(Settings().inputExpanded ? PGX::Visibility::Visible : PGX::Visibility::Collapsed);

        body.Children().Append(MakeEndpointList(context, context->data->capture, false));

        head.Tapped([arrow, body](PGF::IInspectable const&, PGX::Input::TappedRoutedEventArgs const&)
        {
            Settings().inputExpanded = !Settings().inputExpanded;
            body.Visibility(Settings().inputExpanded ? PGX::Visibility::Visible : PGX::Visibility::Collapsed);
            arrow.Text(Settings().inputExpanded ? L"\xE70E" : L"\xE70D");
        });

        PGXC::StackPanel group;
        group.Children().Append(head);
        group.Children().Append(body);
        layer.Children().Append(group);
        return layer;
    }

    inline bool SendPipeLine(const std::string& line) { return TapPageSendPipe(line.c_str()) != 0; }

    inline void SendOrLog(const std::string& line, std::wstring_view verb)
    {
        if (!SendPipeLine(line))
        {
            LogKey(L"log.page.commandDropped", { std::wstring(verb) });
        }
    }

    inline void SendSetRedirect(std::uint32_t processId, const std::wstring& deviceId)
    {
        SendOrLog("SETREDIRECT render " + std::to_string(processId) + " " + Narrow(deviceId) + "\n", L"SETREDIRECT");
    }

    [[nodiscard]] inline winrt::hstring InitialLetter(const std::wstring& name)
    {
        for (const wchar_t value : name)
        {
            if (::iswalnum(value) != 0)
            {
                return winrt::hstring(std::wstring(1, static_cast<wchar_t>(::towupper(value))));
            }
        }
        return winrt::hstring(L"?");
    }

    [[nodiscard]] inline PGX::FrameworkElement MakeAppChip(const std::wstring& name)
    {
        PGXC::Grid holder;
        holder.Width(20);
        holder.Height(20);
        holder.Margin(PGX::ThicknessHelper::FromLengths(0, 0, 10, 0));
        holder.VerticalAlignment(PGX::VerticalAlignment::Center);

        PGXSH::Rectangle chip;
        chip.RadiusX(4);
        chip.RadiusY(4);
        chip.Fill(PGXM::SolidColorBrush(PGUI::ColorHelper::FromArgb(0x40, 0xA0, 0xA0, 0xA0)));
        holder.Children().Append(chip);

        PGXC::TextBlock letter = Text(InitialLetter(name), 10);
        letter.HorizontalAlignment(PGX::HorizontalAlignment::Center);
        letter.VerticalAlignment(PGX::VerticalAlignment::Center);
        letter.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        holder.Children().Append(letter);
        return holder;
    }

    [[nodiscard]] inline PGX::FrameworkElement MakeSystemSoundIcon()
    {
        PGXC::Grid holder;
        holder.Width(20);
        holder.Height(20);
        holder.Margin(PGX::ThicknessHelper::FromLengths(0, 0, 10, 0));
        holder.VerticalAlignment(PGX::VerticalAlignment::Center);

        PGXSH::Path icon;
        icon.Width(16);
        icon.Height(16);
        icon.Stretch(PGXM::Stretch::Uniform);
        icon.Fill(AccentBrush());
        PGXM::PathGeometry geometry;
        PGXM::PathFigure figure;
        figure.StartPoint(PGF::Point{ 3, 9 });
        for (const PGF::Point point : { PGF::Point{ 7, 9 }, PGF::Point{ 12, 5 },
                                        PGF::Point{ 12, 19 }, PGF::Point{ 7, 15 }, PGF::Point{ 3, 15 } })
        {
            PGXM::LineSegment segment;
            segment.Point(point);
            figure.Segments().Append(segment);
        }
        figure.IsClosed(true);
        geometry.Figures().Append(figure);
        icon.Data(geometry);
        icon.HorizontalAlignment(PGX::HorizontalAlignment::Center);
        icon.VerticalAlignment(PGX::VerticalAlignment::Center);
        holder.Children().Append(icon);
        return holder;
    }

    [[nodiscard]] inline PGX::FrameworkElement MakeAppIcon(const audio::SessionInfo& session)
    {
        if (session.appKey == L"#system")
        {
            return MakeSystemSoundIcon();
        }

        std::wstring iconFile;
        if (session.processId != 0 &&
            icons::FileForProcess(session.processId, platform::GetCacheDirectory(), iconFile).IsOk())
        {
            std::wstring uri(L"file:///");
            for (const wchar_t value : iconFile)
            {
                uri.push_back(value == L'\\' ? L'/' : value);
            }

            PGXMI::BitmapImage source;
            source.UriSource(PGF::Uri(winrt::hstring(uri)));
            source.DecodePixelWidth(40);

            PGXC::Image image;
            image.Source(source);
            image.Width(20);
            image.Height(20);
            image.Stretch(PGXM::Stretch::Uniform);
            image.VerticalAlignment(PGX::VerticalAlignment::Center);
            image.Margin(PGX::ThicknessHelper::FromLengths(0, 0, 10, 0));
            return image;
        }

        return MakeAppChip(session.displayName);
    }

    [[nodiscard]] inline const std::wstring& RedirectSelection(ContextPtr const& context, const std::wstring& appKey)
    {
        const auto found = context->redirects.find(appKey);
        if (found != context->redirects.end())
        {
            return found->second;
        }

        return context->redirects.emplace(appKey, std::wstring()).first->second;
    }

    [[nodiscard]] inline std::wstring RedirectTargetName(ContextPtr const& context, const std::wstring& appKey)
    {
        const std::wstring& selected = RedirectSelection(context, appKey);
        if (selected.empty())
        {
            return std::wstring();
        }

        for (const auto& device : context->data->render)
        {
            if (device.id == selected)
            {
                return audio::DisplayDeviceName(device, Settings().showDriverName);
            }
        }

        return selected;
    }

    struct RouteLine final
    {
        PGXC::Grid row{nullptr};
        PGXC::TextBlock name{nullptr};

        void Update(std::wstring const& target) const
        {
            name.Text(winrt::hstring(target));

            if (target.empty())
            {
                PGXC::ToolTipService::SetToolTip(name, nullptr);
                row.Visibility(PGX::Visibility::Collapsed);
                return;
            }

            PGXC::ToolTipService::SetToolTip(name, winrt::box_value(winrt::hstring(target)));
            row.Visibility(PGX::Visibility::Visible);
        }
    };

    [[nodiscard]] inline RouteLine MakeRouteLine(ContextPtr const& context, const std::wstring& appKey)
    {
        PGXC::ColumnDefinition arrowColumn;
        PGXC::ColumnDefinition targetColumn;
        arrowColumn.Width(PGX::GridLengthHelper::Auto());
        targetColumn.Width(PGX::GridLengthHelper::FromValueAndType(1, PGX::GridUnitType::Star));

        RouteLine line;
        line.row = PGXC::Grid();
        line.row.ColumnDefinitions().Append(arrowColumn);
        line.row.ColumnDefinitions().Append(targetColumn);
        line.row.VerticalAlignment(PGX::VerticalAlignment::Center);
        line.row.Margin(PGX::ThicknessHelper::FromLengths(0, 1, 0, 0));

        const auto accent = AccentBrush();

        PGXC::TextBlock arrow = Text(L"\x2192", 11);
        arrow.Opacity(0.75);
        arrow.Foreground(accent);
        arrow.Margin(PGX::ThicknessHelper::FromLengths(0, 0, 5, 0));
        PGXC::Grid::SetColumn(arrow, 0);
        line.row.Children().Append(arrow);

        PGXC::TextBlock name = Text(winrt::hstring(), 11);
        name.Foreground(accent);
        name.TextWrapping(PGX::TextWrapping::NoWrap);
        PGXC::Grid::SetColumn(name, 1);
        line.row.Children().Append(name);
        line.name = name;

        line.Update(RedirectTargetName(context, appKey));
        LogKey(L"log.page.appRoute", { appKey, std::wstring(line.name.Text()) });
        return line;
    }

    [[nodiscard]] inline PGXC::ComboBox MakeRedirectCombo(ContextPtr const& context, const audio::SessionInfo& session,
                                                        std::function<void(std::wstring const&)> onPicked)
    {
        PGXC::ComboBox combo;
        combo.HorizontalAlignment(PGX::HorizontalAlignment::Stretch);
        combo.MinHeight(32);

        const std::wstring current = RedirectSelection(context, session.appKey);

        PGXC::ComboBoxItem none;
        none.Content(winrt::box_value(winrt::hstring(text::Embedded().Resolve(L"page.app.redirectDefault"))));
        none.Tag(winrt::box_value(winrt::hstring(L"")));
        combo.Items().Append(none);
        if (current.empty())
        {
            combo.SelectedIndex(0);
        }

        for (std::size_t i = 0; i < context->data->render.size(); ++i)
        {
            const auto& device = context->data->render[i];
            PGXC::ComboBoxItem item;
            item.Content(winrt::box_value(winrt::hstring(audio::DisplayDeviceName(device, Settings().showDriverName))));
            item.Tag(winrt::box_value(winrt::hstring(device.id)));
            combo.Items().Append(item);

            if (device.id == current)
            {
                combo.SelectedIndex(static_cast<int>(i) + 1);
            }
        }

        const std::vector<audio::DeviceInfo> snapshot = context->data->render;
        const std::uint32_t processId = session.processId;
        const std::wstring appKey = session.appKey;
        combo.DropDownOpened([appKey](PGF::IInspectable const&, PGF::IInspectable const&)
        {
            LogKey(L"log.page.appRedirectOpened", { appKey });
        });
        combo.DropDownClosed([appKey](PGF::IInspectable const&, PGF::IInspectable const&)
        {
            LogKey(L"log.page.appRedirectClosed", { appKey });
        });
        combo.SelectionChanged([snapshot, processId, appKey, context, onPicked](PGF::IInspectable const& sender,
                                                                             PGXC::SelectionChangedEventArgs const&)
        {
            if (auto self = sender.try_as<PGXC::ComboBox>())
            {
                const int index = self.SelectedIndex();
                if (index <= 0)
                {
                    context->redirects[appKey] = std::wstring();
                    LogKey(L"log.page.appRedirectPick", { std::to_wstring(processId), L"(默认)" });
                    SendSetRedirect(processId, std::wstring());
                    onPicked(std::wstring());
                    return;
                }

                const std::size_t position = static_cast<std::size_t>(index) - 1;
                if (position < snapshot.size())
                {
                    context->redirects[appKey] = snapshot[position].id;
                    LogKey(L"log.page.appRedirectPick", { std::to_wstring(processId), snapshot[position].friendlyName });
                    SendSetRedirect(processId, snapshot[position].id);
                    onPicked(audio::DisplayDeviceName(snapshot[position], Settings().showDriverName));
                }
            }
        });
        return combo;
    }

    inline PGXC::StackPanel MakeAppRow(ContextPtr const& context, const audio::SessionInfo& session, audio::IAudioSessionHandle* handle)
    {
        const std::wstring appKey = session.appKey;
        const bool movable = session.processId != 0 && !appKey.empty() && appKey != L"#system";
        PGXC::Grid grid = Row(44);
        grid.Margin(PGX::ThicknessHelper::FromLengths(0, 0, 12, 0));

        PGXC::Grid titleBand;
        PGXC::ColumnDefinition leading;
        PGXC::ColumnDefinition flexible;
        leading.Width(PGX::GridLengthHelper::Auto());
        flexible.Width(PGX::GridLengthHelper::FromValueAndType(1, PGX::GridUnitType::Star));
        titleBand.ColumnDefinitions().Append(leading);
        titleBand.ColumnDefinitions().Append(flexible);

        PGX::FrameworkElement icon = MakeAppIcon(session);
        PGXC::Grid::SetColumn(icon, 0);
        titleBand.Children().Append(icon);

        PGXC::StackPanel block;
        block.VerticalAlignment(PGX::VerticalAlignment::Center);

        PGXC::TextBlock name = Text(winrt::hstring(session.displayName), 13);
        name.TextWrapping(PGX::TextWrapping::WrapWholeWords);
        name.MaxLines(2);
        PGXC::ToolTipService::SetToolTip(name, winrt::box_value(winrt::hstring(session.displayName)));
        block.Children().Append(name);

        RouteLine route{};
        if (movable)
        {
            route = MakeRouteLine(context, appKey);
            block.Children().Append(route.row);
        }

        PGXC::Grid::SetColumn(block, 1);
        titleBand.Children().Append(block);

        PGXC::Grid::SetColumn(titleBand, 0);
        PGXC::Grid::SetColumnSpan(titleBand, 2);
        grid.Children().Append(titleBand);

        PGXC::StackPanel trailing;
        trailing.Orientation(PGXC::Orientation::Horizontal);
        trailing.VerticalAlignment(PGX::VerticalAlignment::Center);

        PGXC::Button mute = MakeMuteButton(session.muted);
        if (handle)
        {
            mute.Click([handle, mute, appKey](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
            {
                float level = 0.0f;
                bool muted = false;
                if (!handle->GetState(level, muted).IsOk()) return;
                if (handle->SetMuted(!muted).IsOk()) SetMuteGlyph(mute, !muted);
                LogKey(L"log.page.appMute", { appKey, muted ? L"取消静音" : L"静音" });
            });
        }
        else
        {
            mute.IsEnabled(false);
        }

        PGXC::Slider slider = MakeVolumeSlider(session.volume);
        if (handle)
        {
            slider.ValueChanged([handle, appKey](PGF::IInspectable const& sender, PGXCP::RangeBaseValueChangedEventArgs const&)
            {
                if (auto s = sender.try_as<PGXC::Slider>())
                {
                    handle->SetVolume(static_cast<float>(s.Value() / 100.0));
                    LogKey(L"log.page.appVolume", { appKey, std::to_wstring(static_cast<int>(s.Value())) });
                }
            });
        }
        else
        {
            slider.IsEnabled(false);
        }
        StereoMeter meter = MakeStereoMeter();
        meter.layer.Children().Append(slider);
        meter.layer.Children().Append(meter.left);
        meter.layer.Children().Append(meter.right);

        PGX::DispatcherTimer meterTimer;
        meterTimer.Interval(std::chrono::milliseconds(50));
        meterTimer.Tick([handle, meter, slider](PGF::IInspectable const&, PGF::IInspectable const&)
        {
            float left = 0.0f;
            float right = 0.0f;
            if (handle->GetPeak(left, right).IsOk())
            {
                UpdateStereoMeter(meter, slider, left, right);
            }
        });
        meterTimer.Start();
        context->meterTimers.push_back(meterTimer);

        PGXC::StackPanel volumeHost;
        volumeHost.Orientation(PGXC::Orientation::Horizontal);
        volumeHost.VerticalAlignment(PGX::VerticalAlignment::Center);
        volumeHost.Children().Append(mute);
        volumeHost.Children().Append(meter.layer);

        PGXC::Grid volumeHostGrid;
        volumeHostGrid.Children().Append(volumeHost);

        PGXCP::ToggleButton routeButton{nullptr};
        PGXC::Grid redirectPanel{nullptr};
        auto comboRef = std::make_shared<PGXC::ComboBox>(nullptr);

        if (movable)
        {
            routeButton = PGXCP::ToggleButton();
            PGXC::TextBlock glyph = Text(winrt::hstring(L"\xE724"), 14);
            glyph.FontFamily(PGXM::FontFamily(L"Segoe MDL2 Assets"));
            routeButton.Content(glyph);
            routeButton.MinHeight(0);
            routeButton.Padding(PGX::ThicknessHelper::FromLengths(8, 4, 8, 4));
            routeButton.Background(nullptr);
            routeButton.BorderThickness(PGX::ThicknessHelper::FromLengths(0, 0, 0, 0));

            redirectPanel = PGXC::Grid();
            redirectPanel.HorizontalAlignment(PGX::HorizontalAlignment::Stretch);
            redirectPanel.VerticalAlignment(PGX::VerticalAlignment::Center);
            redirectPanel.Visibility(PGX::Visibility::Collapsed);
            volumeHostGrid.Children().Append(redirectPanel);

            trailing.Children().Append(routeButton);
            trailing.Children().Append(volumeHostGrid);

            routeButton.Checked([volumeHost, redirectPanel, comboRef, context, session, route](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
            {
                if (!*comboRef)
                {
                    *comboRef = MakeRedirectCombo(context, session,
                        [route](std::wstring const& target) { route.Update(target); });
                    redirectPanel.Children().Append(*comboRef);
                }
                volumeHost.Opacity(0);
                volumeHost.IsHitTestVisible(false);
                redirectPanel.Visibility(PGX::Visibility::Visible);
                LogKey(L"log.page.appRowToggle", { L"展开" });
            });

            routeButton.Unchecked([volumeHost, redirectPanel, comboRef](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
            {
                if (*comboRef)
                {
                    (*comboRef).IsDropDownOpen(false);
                }
                redirectPanel.Visibility(PGX::Visibility::Collapsed);
                volumeHost.Opacity(1);
                volumeHost.IsHitTestVisible(true);
                LogKey(L"log.page.appRowToggle", { L"收起" });
            });
        }
        else
        {
            trailing.Children().Append(volumeHostGrid);
        }

        PGXC::Grid::SetColumn(trailing, 2);
        grid.Children().Append(trailing);

        PGXC::StackPanel container;
        container.Children().Append(grid);
        return container;
    }

    inline PGXC::StackPanel BuildAppLayer(ContextPtr const& context)
    {
        PGXC::StackPanel layer;
        layer.Children().Append(SectionLabel(winrt::hstring(text::Embedded().Resolve(L"page.section.mixer"))));

        if (context->data->apps.empty())
        {
            PGXC::TextBlock none = Text(winrt::hstring(text::Embedded().Resolve(L"page.none.apps")), 12);
            none.Opacity(0.5);
            none.Margin(PGX::ThicknessHelper::FromLengths(8, 2, 4, 8));
            layer.Children().Append(none);
            LogKey(L"log.page.appsEmpty", { context->data->appsEndpoint });
        }

        for (std::size_t i = 0; i < context->data->apps.size(); ++i)
        {
            audio::IAudioSessionHandle* handle =
                (i < context->data->handles.size()) ? context->data->handles[i].get() : nullptr;
            layer.Children().Append(MakeAppRow(context, context->data->apps[i], handle));
        }
        return layer;
    }

    inline void SendSetDefault(bool render, const std::wstring& deviceId)
    {
        SendOrLog(std::string("SETDEFAULT ") + (render ? "render " : "capture ") + Narrow(deviceId) + "\n", L"SETDEFAULT");
    }

    inline void SendClearRedirects() { SendOrLog("CLEARREDIRECT\n", L"CLEARREDIRECT"); }

    inline void SendUninstall() { SendOrLog("UNINSTALL\n", L"UNINSTALL"); }
    inline void SendExit() { SendOrLog("EXIT\n", L"EXIT"); }

    inline void OpenUrl(const std::wstring& url)
    {
        const auto result = reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32)
        {
            LogKey(L"log.page.openUrlFailed", { std::to_wstring(static_cast<long long>(result)) });
        }
    }

    inline PGXC::StackPanel BuildCustomPage(ContextPtr const& context);

    inline PGXC::StackPanel BuildSettingsPage(ContextPtr const& context);

    inline void MountFooter(ContextPtr const& context, int attempt);

    inline void Settle(ContextPtr const& context, int attempt);
    inline void CommitTakeover(ContextPtr const& context);

    inline void InstallFooterWatch(ContextPtr const& context);

    inline void RefreshFooter(ContextPtr const& context)
    {
        if (!context->slotLeft) return;

        const bool custom = (Settings().page == 0);
        const bool settings = (Settings().page == 1);

        const PGX::Visibility twoSlots = (custom || settings) ? PGX::Visibility::Visible : PGX::Visibility::Collapsed;
        context->slotLeft.Visibility(twoSlots);
        context->slotMid.Visibility(twoSlots);
        if (context->systemItem)
        {
            context->systemItem.Visibility((custom || settings) ? PGX::Visibility::Collapsed : PGX::Visibility::Visible);
        }

        context->slotLeft.Content(winrt::box_value(winrt::hstring(text::Embedded().Resolve(
            settings ? L"page.footer.github" : L"page.footer.clearRedirect"))));
        context->slotMid.Content(winrt::box_value(winrt::hstring(text::Embedded().Resolve(
            settings ? L"page.footer.back" : L"page.footer.settings"))));
        context->slotRight.Content(winrt::box_value(custom ? L"SystemMixer" : L"MixerExtender"));
        context->slotRight.Visibility(PGX::Visibility::Visible);
    }

    inline void MountPage(ContextPtr const& context, int page)
    {
        Settings().page = page;
        if (!context->list) return;

        std::wstring name = text::Embedded().Resolve(L"log.page.name.system");
        if (page == 0)
        {
            name = text::Embedded().Resolve(L"log.page.name.custom");
            context->list.Content(BuildCustomPage(context));
        }
        else if (page == 1)
        {
            name = text::Embedded().Resolve(L"log.page.name.settings");
            context->list.Content(BuildSettingsPage(context));
        }
        else
        {
            context->list.Content(context->savedContent);
        }
        RefreshFooter(context);

        LogKey(L"log.page.mountPage", { std::to_wstring(page), name });
    }

    inline void InstallFooter(ContextPtr const& context)
    {
        context->slotLeft = FooterButton(winrt::hstring(text::Embedded().Resolve(L"page.footer.clearRedirect")));
        context->slotMid = FooterButton(winrt::hstring(text::Embedded().Resolve(L"page.footer.settings")));
        context->slotRight = FooterButton(L"SystemMixer");

        ContextPtr weakContext = context;
        context->slotLeft.Click([weakContext](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
        {
            if (Settings().page == 1)
            {
                OpenUrl(L"https://github.com/EricZhang233/VolumeMixerExtender");
            }
            else if (Settings().page == 0)
            {
                SendClearRedirects();
                weakContext->redirects.clear();
                MountPage(weakContext, 0);
                LogKey(L"log.page.clearRedirects");
            }
        });
        context->slotMid.Click([weakContext](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
        {
            if (Settings().page == 0) MountPage(weakContext, 1);
            else if (Settings().page == 1) MountPage(weakContext, 0);
        });
        context->slotRight.Click([weakContext](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
        {
            if (Settings().page == 0) MountPage(weakContext, 2);
            else if (Settings().page == 2) MountPage(weakContext, 0);
        });
    }

    inline void LoadData(ContextPtr const& context)
    {
        PageData& data = *context->data;
        if (!context->manager) return;

        context->manager->EnumerateDevices(audio::DataFlow::Render, audio::DeviceState::Active, data.render);
        context->manager->EnumerateDevices(audio::DataFlow::Capture, audio::DeviceState::Active, data.capture);

        audio::DeviceInfo device;
        if (context->manager->GetDefaultDevice(audio::DataFlow::Render, audio::DeviceRole::Multimedia, device).IsOk())
        {
            data.defaultRender = device.id;
        }
        if (context->manager->GetDefaultDevice(audio::DataFlow::Capture, audio::DeviceRole::Multimedia, device).IsOk())
        {
            data.defaultCapture = device.id;
        }

        data.appsEndpoint = data.defaultRender;
        if (data.appsEndpoint.empty() && !data.render.empty()) data.appsEndpoint = data.render.front().id;

        if (!data.appsEndpoint.empty())
        {
            context->manager->EnumerateSessions(data.appsEndpoint, data.apps);

            data.handles.reserve(data.apps.size());
            for (const auto& session : data.apps)
            {
                if (context->endpointPolicy && session.processId != 0 && !session.appKey.empty() &&
                    session.appKey != L"#system")
                {
                    std::wstring persisted;
                    if (context->endpointPolicy->GetAppDefaultDevice(
                            session.processId, audio::DataFlow::Render, audio::DeviceRole::Multimedia, persisted).IsOk() &&
                        !persisted.empty() && persisted != data.defaultRender)
                    {
                        context->redirects[session.appKey] = std::move(persisted);
                    }
                    else
                    {
                        context->redirects[session.appKey] = std::wstring();
                    }
                }

                std::unique_ptr<audio::IAudioSessionHandle> handle;
                if (context->manager->OpenSession(data.appsEndpoint, session.instanceId, handle).IsOk())
                {
                    data.handles.push_back(std::move(handle));
                }
                else
                {
                    data.handles.push_back(nullptr);
                }
            }
        }

        LogKey(L"log.page.snapshot", { std::to_wstring(data.render.size()), std::to_wstring(data.capture.size()),
                                       std::to_wstring(data.apps.size()), data.appsEndpoint });
    }

    inline PGX::FrameworkElement FindByNameDeep(PGX::DependencyObject const& node, wchar_t const* want, int depth)
    {
        if (depth > 14) return nullptr;

        UINT32 count = 0;
        try { count = PGXM::VisualTreeHelper::GetChildrenCount(node); } catch (...) { return nullptr; }

        for (UINT32 i = 0; i < count; ++i)
        {
            PGX::DependencyObject child{nullptr};
            try { child = PGXM::VisualTreeHelper::GetChild(node, i); } catch (...) { continue; }

            if (auto element = child.try_as<PGX::FrameworkElement>())
            {
                try { if (::wcscmp(element.Name().c_str(), want) == 0) return element; } catch (...) {}
            }
            if (auto found = FindByNameDeep(child, want, depth + 1)) return found;
        }
        return nullptr;
    }

    inline constexpr unsigned long long kEntryWindowMs = 5000;

    inline void ExpectSoundPage()
    {
        Settings().expectSoundPage = true;
        Settings().expectTick = ::GetTickCount64();
        LogKey(L"log.page.entryExpect");
    }

    inline bool ConsumeSoundPageExpectation()
    {
        if (!Settings().expectSoundPage) return false;
        Settings().expectSoundPage = false;

        const unsigned long long age = ::GetTickCount64() - Settings().expectTick;
        return age <= kEntryWindowMs;
    }

    inline PGX::FrameworkElement FindByMarker(PGX::DependencyObject const& node, wchar_t const* want, int depth)
    {
        if (!node || depth > 32) return nullptr;

        UINT32 count = 0;
        try { count = PGXM::VisualTreeHelper::GetChildrenCount(node); } catch (...) { return nullptr; }

        for (UINT32 i = 0; i < count; ++i)
        {
            PGX::DependencyObject child{nullptr};
            try { child = PGXM::VisualTreeHelper::GetChild(node, i); } catch (...) { continue; }

            if (auto element = child.try_as<PGX::FrameworkElement>())
            {
                try { if (::wcscmp(element.Name().c_str(), want) == 0) return element; } catch (...) {}
                try
                {
                    const auto id = PGXAU::AutomationProperties::GetAutomationId(element);
                    if (!id.empty() && ::wcscmp(id.c_str(), want) == 0) return element;
                }
                catch (...) {}
            }
            if (auto found = FindByMarker(child, want, depth + 1)) return found;
        }
        return nullptr;
    }

    inline std::wstring PageTitle(PGX::FrameworkElement const& pageWindow)
    {
        PGX::FrameworkElement header = FindByNameDeep(pageWindow, L"PageHeader", 0);
        if (!header) return {};

        PGXC::TextBlock title = FindByMarker(header, L"PageTitleText", 0).try_as<PGXC::TextBlock>();
        if (!title) title = FindByMarker(header, L"PageTitle", 0).try_as<PGXC::TextBlock>();
        if (!title) return {};

        try { return std::wstring(title.Text()); } catch (...) { return {}; }
    }

    inline bool IsSoundPageContent(PGX::FrameworkElement const& pageWindow)
    {
        for (auto const* marker : {L"OutputGroupTitle", L"MixerGroupTitle", L"SpatialGroupTitle",
                                   L"ListWithOutputGroupTitle"})
        {
            if (FindByMarker(pageWindow, marker, 0)) return true;
        }
        return false;
    }

    inline bool TakeOver(PGX::FrameworkElement const& footer)
    {
        PGX::FrameworkElement pageWindow{nullptr};
        {
            PGX::DependencyObject current = footer;
            for (int i = 0; i < 14 && current; ++i)
            {
                PGX::DependencyObject parent{nullptr};
                try { parent = PGXM::VisualTreeHelper::GetParent(current); } catch (...) { break; }
                if (!parent) break;
                if (auto element = parent.try_as<PGX::FrameworkElement>())
                {
                    try { if (element.Name() == L"PageWindow") { pageWindow = element; break; } } catch (...) {}
                }
                current = parent;
            }
        }

        if (!pageWindow)
        {
            LogKey(L"log.page.noPageWindow");
            return false;
        }

        PGX::FrameworkElement listElement = FindByNameDeep(pageWindow, L"ListContent", 0);
        PGXC::ScrollViewer scroller = listElement ? listElement.try_as<PGXC::ScrollViewer>() : nullptr;
        if (!scroller)
        {
            LogKey(L"log.page.noListContent");
            return false;
        }

        auto context = std::make_shared<Context>();
        context->manager = audio::CreateAudioDeviceManager();
        context->endpointPolicy = audio::CreateEndpointPolicyService();
        context->footer = footer;
        context->pageWindow = pageWindow;
        context->list = scroller;
        context->data = std::make_shared<PageData>();
        context->startedTick = ::GetTickCount64();

        InstallFooterWatch(context);

        if (ConsumeSoundPageExpectation())
        {
            CommitTakeover(context);
            return true;
        }

        LogKey(L"log.page.waitContent");
        Settle(context, 0);
        return true;
    }

    inline void CommitTakeover(ContextPtr const& context)
    {
        context->committed = true;

        Settings().showDriverName = settings::ReadBool(settings::kShowDriverName, true);

        try { context->savedContent = context->list.Content(); } catch (...) {}

        LoadData(context);
        InstallFooter(context);
        MountPage(context, 0);

        MountFooter(context, 0);
        LogKey(L"log.page.takeover", { context->savedContent
                                           ? text::Embedded().Resolve(L"log.page.saved")
                                           : text::Embedded().Resolve(L"log.page.empty") });
    }

    inline void Settle(ContextPtr const& context, int attempt)
    {
        if (context->abandoned) return;

        if (context->committed)
        {
            MountFooter(context, attempt);
            return;
        }

        constexpr unsigned long long kIdentifyIntervalMs = 25;
        const unsigned long long now = ::GetTickCount64();
        if (now - context->lastIdentifyTick >= kIdentifyIntervalMs)
        {
            context->lastIdentifyTick = now;
            if (IsSoundPageContent(context->pageWindow))
            {
                LogKey(L"log.page.contentDetected");
                CommitTakeover(context);
                return;
            }
        }

        constexpr unsigned long long kIdentifyBudgetMs = 3000;
        const unsigned long long elapsed = now - context->startedTick;
        if (elapsed > kIdentifyBudgetMs)
        {
            context->abandoned = true;
            const std::wstring title = PageTitle(context->pageWindow);
            LogKey(L"log.page.notSoundPage", { title, std::to_wstring(elapsed) });
            PGX::FrameworkElement list = FindByNameDeep(context->pageWindow, L"ListContent", 0);
            TapPageDumpTree(list ? list : context->pageWindow, 6);
            return;
        }

        auto dispatcher = context->footer.Dispatcher();
        if (!dispatcher) return;

        ContextPtr retry = context;
        try
        {
            dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Low,
                                [retry]() { Settle(retry, 0); });
        }
        catch (...) {}
    }


    inline bool TryMountNow(ContextPtr const& context)
    {
        if (context->mountDone) return true;
        if (context->mounting) return false;

        context->mounting = true;
        const FooterMount mount = TapPageMountFooterRow(
            context->footer, { context->slotLeft, context->slotMid, context->slotRight });
        context->mounting = false;

        if (mount.ok)
        {
            context->systemItem = mount.systemItem;
            context->mountDone = true;
            RefreshFooter(context);
            LogKey(L"log.page.footerMounted", { std::to_wstring(::GetTickCount64() - context->startedTick) });
            return true;
        }

        const std::wstring state = TapPageDescribeNode(context->footer);
        if (state != context->lastFooterState)
        {
            LogKey(L"log.page.footerMountFailed", { std::to_wstring(context->mountAttempts),
                                                    std::to_wstring(::GetTickCount64() - context->startedTick),
                                                    strings::ToWide(mount.reason), state });
            context->lastFooterState = state;
        }
        context->mountAttempts += 1;
        return false;
    }

    inline void MountFooter(ContextPtr const& context, int attempt)
    {
        if (TryMountNow(context)) return;

        constexpr unsigned long long kBudgetMs = 6000;
        const unsigned long long elapsed = ::GetTickCount64() - context->startedTick;
        if (elapsed > kBudgetMs)
        {
            if (!context->mountGaveUp)
            {
                context->mountGaveUp = true;
                LogKey(L"log.page.footerGaveUp",
                       { std::to_wstring(elapsed), std::to_wstring(context->mountAttempts) });
                TapPageDumpTree(context->footer, 14);
            }
            return;
        }

        auto dispatcher = context->footer.Dispatcher();
        if (!dispatcher)
        {
            LogKey(L"log.page.noDispatcher");
            return;
        }

        if (context->mountChainRunning) return;
        context->mountChainRunning = true;

        ContextPtr retryContext = context;
        try
        {
            dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Low,
                                [retryContext, attempt]()
                                {
                                    retryContext->mountChainRunning = false;
                                    MountFooter(retryContext, attempt + 1);
                                });
        }
        catch (...)
        {
            context->mountChainRunning = false;
            LogKey(L"log.page.dispatchFailed");
        }
    }

    inline void InstallFooterWatch(ContextPtr const& context)
    {
        std::weak_ptr<Context> weak = context;

        context->footer.SizeChanged([weak](PGF::IInspectable const&, PGX::SizeChangedEventArgs const& args)
        {
            if (auto live = weak.lock())
            {
                LogKey(L"log.page.footerSizeChanged", { strings::FromDouble(args.NewSize().Width, 0),
                                                        strings::FromDouble(args.NewSize().Height, 0) });
                Settle(live, 0);
            }
        });

        context->footer.LayoutUpdated([weak](PGF::IInspectable const&, PGF::IInspectable const&)
        {
            if (auto live = weak.lock())
            {
                if (live->mountDone) return;
                live->layoutTicks += 1;
                if (live->layoutTicks == 1)
                {
                    LogKey(L"log.page.footerFirstLayout");
                }
                Settle(live, 0);
            }
        });
    }

    inline PGXC::StackPanel BuildCustomPage(ContextPtr const& context)
    {
        PGXC::StackPanel root;
        root.Padding(PGX::ThicknessHelper::FromLengths(kBodyInset, 0, kBodyInset, 0));
        Settings().recordingMode = false;
        root.Children().Append(BuildVolumeLayer(context));
        root.Children().Append(BuildAppLayer(context));
        return root;
    }

    inline PGXC::StackPanel BuildSettingsPage(ContextPtr const& context)
    {
        PGXC::StackPanel root;
        root.Padding(PGX::ThicknessHelper::FromLengths(kBodyInset, 0, kBodyInset, 0));
        root.Children().Append(SectionLabel(winrt::hstring(text::Embedded().Resolve(L"page.section.general"))));

        {
            PGXC::Grid row = Row(44);
            PGXC::StackPanel block = NameBlock(text::Embedded().Resolve(L"page.toggle.autostart"), std::wstring());
            PGXC::Grid::SetColumn(block, 1);
            row.Children().Append(block);

            PGXC::ToggleSwitch toggle;
            toggle.OnContent(nullptr);
            toggle.OffContent(nullptr);
            toggle.MinWidth(0);
            toggle.IsOn(autostart::AutostartEntry::IsEnabled());
            toggle.HorizontalAlignment(PGX::HorizontalAlignment::Right);
            toggle.VerticalAlignment(PGX::VerticalAlignment::Center);
            PGXC::Grid::SetColumn(toggle, 2);
            row.Children().Append(toggle);

            auto busy = std::make_shared<bool>(false);
            toggle.Toggled([toggle, busy](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
            {
                if (*busy) return;
                *busy = true;

                const bool wanted = toggle.IsOn();
                SendOrLog(wanted ? "AUTOSTART on\n" : "AUTOSTART off\n", L"AUTOSTART");

                bool actual = autostart::AutostartEntry::IsEnabled();
                for (int attempt = 0; attempt < 20 && actual != wanted; ++attempt)
                {
                    ::Sleep(50);
                    actual = autostart::AutostartEntry::IsEnabled();
                }
                if (toggle.IsOn() != actual) toggle.IsOn(actual);

                LogKey(L"log.page.autostart", {
                    wanted ? text::Embedded().Resolve(L"log.page.autostart.enable")
                           : text::Embedded().Resolve(L"log.page.autostart.remove"),
                    actual ? text::Embedded().Resolve(L"log.page.autostart.enabled")
                           : text::Embedded().Resolve(L"log.page.autostart.missing") });
                *busy = false;
            });
            root.Children().Append(row);
        }

        {
            PGXC::Grid row = Row(44);
            PGXC::StackPanel block = NameBlock(text::Embedded().Resolve(L"page.toggle.showDriverName"), std::wstring());
            PGXC::Grid::SetColumn(block, 1);
            row.Children().Append(block);

            PGXC::ToggleSwitch toggle;
            toggle.OnContent(nullptr);
            toggle.OffContent(nullptr);
            toggle.MinWidth(0);
            toggle.IsOn(Settings().showDriverName);
            toggle.HorizontalAlignment(PGX::HorizontalAlignment::Right);
            toggle.VerticalAlignment(PGX::VerticalAlignment::Center);
            PGXC::Grid::SetColumn(toggle, 2);
            row.Children().Append(toggle);

            ContextPtr weakContext = context;
            toggle.Toggled([weakContext, toggle](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
            {
                Settings().showDriverName = toggle.IsOn();
                settings::WriteBool(settings::kShowDriverName, Settings().showDriverName);
                LogKey(L"log.page.showDriverName", { Settings().showDriverName ? L"1" : L"0" });

                MountPage(weakContext, Settings().page);
            });
            root.Children().Append(row);
        }

        PGXC::Grid spacer;
        spacer.Height(24);
        root.Children().Append(spacer);

        PGXC::Button exit = DangerButton(winrt::hstring(text::Embedded().Resolve(L"page.button.exit")));
        exit.HorizontalAlignment(PGX::HorizontalAlignment::Stretch);
        exit.Click([exit](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
        {
            exit.IsEnabled(false);
            exit.Content(winrt::box_value(winrt::hstring(text::Embedded().Resolve(L"page.button.exiting"))));
            SendExit();
        });
        root.Children().Append(exit);

        PGXC::Button danger = DangerButton(winrt::hstring(text::Embedded().Resolve(L"page.button.uninstall")));
        danger.HorizontalAlignment(PGX::HorizontalAlignment::Stretch);

        auto hits = std::make_shared<int>(0);
        auto timer = std::make_shared<PGX::DispatcherTimer>();
        timer->Interval(std::chrono::milliseconds(500));
        timer->Tick([hits, timer, danger](PGF::IInspectable const&, PGF::IInspectable const&)
        {
            timer->Stop();
            *hits = 0;
            danger.Content(winrt::box_value(winrt::hstring(text::Embedded().Resolve(L"page.button.uninstall"))));
            ApplyDangerVisual(danger, false);
        });

        danger.Click([hits, timer, danger](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
        {
            ++(*hits);
            if (*hits >= 5)
            {
                timer->Stop();
                *hits = 0;
                danger.IsEnabled(false);
                danger.Content(winrt::box_value(winrt::hstring(text::Embedded().Resolve(L"page.button.uninstalling"))));
                ApplyDangerVisual(danger, false);
                SendUninstall();
                return;
            }

            danger.Content(winrt::box_value(winrt::hstring(text::Embedded().ResolveFormat(
                L"page.button.uninstallCount", { std::to_wstring(5 - *hits) }))));

            ApplyDangerVisual(danger, true);

            timer->Stop();
            timer->Start();
        });
        root.Children().Append(danger);
        return root;
    }
}
