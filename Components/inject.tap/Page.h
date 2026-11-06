#pragma once

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
#include "RecordingClient.h"
#include "RecordingPanelState.h"
#include "RecordingDriverService.h"
#include "PanelPresentation.h"

#include <windows.h>
#include <shellapi.h>

#undef GetCurrentTime

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <cstdint>
#include <cmath>
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

extern int TapPageSendPipe(const char* line);

struct FooterMount final
{
    winrt::Windows::UI::Xaml::UIElement systemItem{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid row{nullptr};
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
    namespace PGXA  = winrt::Windows::UI::Xaml::Media::Animation;
    namespace PGXMI = winrt::Windows::UI::Xaml::Media::Imaging;
    namespace PGXSH = winrt::Windows::UI::Xaml::Shapes;
    namespace PGXAU = winrt::Windows::UI::Xaml::Automation;
    namespace PGUI  = winrt::Windows::UI;
    namespace PGF   = winrt::Windows::Foundation;
    namespace PGXIN = winrt::Windows::UI::Xaml::Input;

    inline void Log(std::wstring_view message, const std::vector<std::wstring>& args = {},
        std::source_location location = std::source_location::current())
    {
        log::Logger::Instance().WriteFormat(log::Level::Debug, L"page", message, args, location);
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

    struct Context;

    struct State final
    {
        bool recordingMode = false;
        bool recordingAvailable = false;
        std::wstring listenEndpoint;
        bool showDriverName = true;
        int page = 0;
        bool inputExpanded = false;

        bool expectSoundPage = false;
        unsigned long long expectTick = 0;

        audio::IAudioDeviceWatcher* watcher = nullptr;
        std::weak_ptr<Context> live;
    };

    [[nodiscard]] inline State& Settings()
    {
        static State state;
        return state;
    }

    struct PageData final : presentation::DeviceSnapshot
    {
        std::vector<audio::AppSessionInfo> apps;
        std::vector<std::shared_ptr<audio::IAudioSessionHandle>> handles;
    };

    struct AppRowBinding final
    {
        std::shared_ptr<std::atomic_bool> dragging = std::make_shared<std::atomic_bool>(false);
        std::shared_ptr<std::atomic_bool> synchronizing = std::make_shared<std::atomic_bool>(false);
        std::shared_ptr<audio::IAudioSessionHandle> handle;
        bool active = true;
        std::wstring appKey;
        PGXC::Slider slider{nullptr};
        PGXC::Button mute{nullptr};
    };

    struct AppRowMotion final
    {
        PGXA::Storyboard animation{nullptr};
        bool removing = false;
    };

    struct EndpointIconPicker final
    {
        PGXC::Button anchor;
        PGXC::Grid row;
        PGXC::StackPanel name;
        PGXC::StackPanel controls;
        PGXC::ScrollViewer view;
        PGXM::SolidColorBrush selectedOutline;
    };

    struct Context;
    using ContextPtr = std::shared_ptr<Context>;
    using presentation::MeterClock;
    using MeterUpdate = std::function<void(const presentation::MeterFrame&)>;

    struct Context final
    {
        std::unique_ptr<audio::IAudioDeviceManager> manager;
        std::unique_ptr<audio::IEndpointPolicyService> endpointPolicy;
        PGX::FrameworkElement footer{nullptr};
        PGXC::ScrollViewer list{nullptr};
        PGX::FrameworkElement pageWindow{nullptr};
        PGF::IInspectable savedContent{nullptr};
        PGXC::Button slotLeft{nullptr};
        PGXC::Button slotEdit{nullptr};
        PGXC::Button slotMid{nullptr};
        PGXC::Button slotRight{nullptr};
        PGXC::Button slotRecording{nullptr};
        PGXC::Grid footerRow{nullptr};
        PGX::UIElement systemItem{nullptr};
        std::map<std::wstring, std::wstring> redirects;
        std::vector<std::shared_ptr<audio::detail::EndpointVolumeMonitor>> endpointMonitors;
        std::vector<std::shared_ptr<MeterUpdate>> appMeters;
        std::vector<std::shared_ptr<MeterUpdate>> endpointMeters;
        std::optional<PGXM::CompositionTarget::Rendering_revoker> meterRendering;
        std::uint64_t meterGeneration = 0;
        PGXC::StackPanel customRoot{nullptr};
        PGXC::Grid volumeLayer{nullptr};
        PGXC::ListView renderRows{nullptr};
        PGXC::ListView captureRows{nullptr};
        std::vector<PGXC::Button> endpointIcons;
        std::optional<EndpointIconPicker> endpointIconPicker;
        PGXC::StackPanel appRows{nullptr};
        std::map<std::wstring, AppRowMotion> appRowMotions;
        PGXC::TextBlock volumeLabel{nullptr};
        std::optional<presentation::DeviceSnapshot> renderedDevices;
        std::vector<std::shared_ptr<AppRowBinding>> rows;
        PGX::DispatcherTimer settleTimer{nullptr};
        PGX::DispatcherTimer deviceTimer{nullptr};
        PGX::DispatcherTimer recordingTimer{nullptr};
        PGX::DispatcherTimer recordingActionTimer{nullptr};
        PGX::DispatcherTimer deviceSelectionTimer{nullptr};
        PGXC::TextBlock recordingFeedback{nullptr};
        bool editing = false;
        bool recordingBusy = false;
        bool recordingChanging = false;
        std::optional<bool> recordingTarget;
        presentation::PendingSelection<bool> pendingRecording;
        presentation::PendingSelection<std::wstring> pendingRenderSelection;
        presentation::PendingSelection<std::wstring> pendingCaptureSelection;
        bool environmentRefreshRequired = false;
        bool recordingAvailable = false;
        bool active = true;
        bool updatingPage = false;
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

    struct PageUpdate final
    {
        explicit PageUpdate(bool& updating) : value(updating) { value = true; }
        ~PageUpdate() { value = false; }
        bool& value;
    };

    inline void StartMeterUpdates(ContextPtr const& context)
    {
        if (context->meterRendering || (context->endpointMeters.empty() && context->appMeters.empty())) return;
        const auto generation = ++context->meterGeneration;
        const std::weak_ptr<Context> weak = context;
        context->meterRendering.emplace(PGXM::CompositionTarget::Rendering(winrt::auto_revoke,
            [weak, generation, cadence = presentation::MeterCadence{}](auto const&, auto const&) mutable
            {
                const auto live = weak.lock();
                if (!live || !live->active || live->meterGeneration != generation) return;
                const auto frame = cadence.Tick(MeterClock::now());
                if (!frame) return;
                for (const auto* rows : { &live->endpointMeters, &live->appMeters })
                {
                    for (std::size_t index = 0; live->meterGeneration == generation && index < rows->size(); ++index)
                    {
                        const auto update = (*rows)[index];
                        (*update)(*frame);
                    }
                }
            }));
        Log(L"Meter rendering started frameLimitHz=60 sampleRateHz=30");
    }

    inline PGXC::TextBlock Text(winrt::hstring const& value, double size)
    {
        PGXC::TextBlock block;
        block.Text(value);
        block.FontSize(size);
        block.TextTrimming(PGX::TextTrimming::CharacterEllipsis);
        return block;
    }

    inline constexpr double kBodyInset = 8.0;
    inline constexpr double kAudioRowLeadingInset = 8.0;
    inline constexpr double kAudioRowTrailingInset = 12.0;
    inline constexpr double kAudioRowIconWidth = 24.0;
    inline constexpr double kAudioRowIconGap = 4.0;
    inline constexpr double kVolumeSliderWidth = 120.0;
    inline constexpr bool kRecordingModeEnabled = true;

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

    inline PGXC::TextBlock Glyph(wchar_t const* code, double size, wchar_t const* font = L"Segoe MDL2 Assets")
    {
        PGXC::TextBlock block = Text(code, size);
        block.FontFamily(PGXM::FontFamily(font));
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

        static thread_local winrt::Windows::UI::ViewManagement::UISettings ui;
        return ui.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent);
    }

    [[nodiscard]] inline PGXM::SolidColorBrush AccentBrush(double opacity = 1.0)
    {
        const auto brush = PGXM::SolidColorBrush(AccentColor());
        if (opacity != 1.0) brush.Opacity(opacity);
        return brush;
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
        slider.Width(kVolumeSliderWidth);
        slider.MinHeight(0);
        slider.VerticalAlignment(PGX::VerticalAlignment::Center);
        return slider;
    }

    inline void ObserveSliderDrag(PGXC::Slider const& slider, std::shared_ptr<std::atomic_bool> const& dragging,
                                  std::function<void()> refreshAfterDrag)
    {
        slider.AddHandler(PGX::UIElement::PointerPressedEvent(), winrt::box_value(PGXIN::PointerEventHandler(
            [dragging](PGF::IInspectable const&, PGXIN::PointerRoutedEventArgs const&)
            {
                dragging->store(true, std::memory_order_release);
            })), true);
        const auto finished = winrt::box_value(PGXIN::PointerEventHandler(
            [refreshAfterDrag](PGF::IInspectable const&, PGXIN::PointerRoutedEventArgs const&)
            {
                refreshAfterDrag();
            }));
        for (const auto& event : { PGX::UIElement::PointerReleasedEvent(), PGX::UIElement::PointerCaptureLostEvent(),
                                   PGX::UIElement::PointerCanceledEvent() })
        {
            slider.AddHandler(event, finished, true);
        }
    }

    struct MeterBand final
    {
        PGXM::PathFigure figure;
        std::array<PGXM::LineSegment, 4> edges;
        std::array<PGXM::QuadraticBezierSegment, 4> corners;
        PGF::Rect bounds{};

        MeterBand()
        {
            figure.IsClosed(true);
            const auto segments = figure.Segments();
            for (std::size_t index = 0; index < edges.size(); ++index)
            {
                segments.Append(edges[index]);
                segments.Append(corners[index]);
            }
        }

        void Update(PGF::Rect next, float topLeft, float topRight, float bottomRight, float bottomLeft)
        {
            if (bounds == next) return;
            bounds = next;
            const auto [x, top, width, height] = bounds;
            const float right = x + width;
            const float bottom = top + height;
            const float limit = std::min(width / 2.0f, height);
            topLeft = std::min(topLeft, limit);
            topRight = std::min(topRight, limit);
            bottomRight = std::min(bottomRight, limit);
            bottomLeft = std::min(bottomLeft, limit);
            const std::array vertices{ PGF::Point{ right, top }, PGF::Point{ right, bottom },
                                       PGF::Point{ x, bottom }, PGF::Point{ x, top } };
            const std::array before{ PGF::Point{ right - topRight, top }, PGF::Point{ right, bottom - bottomRight },
                                     PGF::Point{ x + bottomLeft, bottom }, PGF::Point{ x, top + topLeft } };
            const std::array after{ PGF::Point{ right, top + topRight }, PGF::Point{ right - bottomRight, bottom },
                                    PGF::Point{ x, bottom - bottomLeft }, PGF::Point{ x + topLeft, top } };
            figure.IsFilled(width > 0);
            figure.StartPoint(after.back());
            for (std::size_t index = 0; index < edges.size(); ++index)
            {
                edges[index].Point(before[index]);
                corners[index].Point1(vertices[index]);
                corners[index].Point2(after[index]);
            }
        }
    };

    struct StereoMeter final
    {
        PGXC::Grid layer{nullptr};
        PGXC::Canvas overlay{nullptr};
        PGX::DependencyObject templateRoot{nullptr};
        PGXSH::Shape track{nullptr};
        PGXSH::Shape volumeFill{nullptr};
        PGX::FrameworkElement thumb{nullptr};
        PGXSH::Path shape{nullptr};
        MeterBand left;
        MeterBand right;
        PGXM::LinearGradientBrush brush{nullptr};
        presentation::MeterLevel leftLevel;
        presentation::MeterLevel rightLevel;

        void Sample(float leftPeak, float rightPeak)
        {
            leftLevel.Sample(leftPeak);
            rightLevel.Sample(rightPeak);
        }
    };

    inline void UpdateMeterBrush(StereoMeter const& meter, PGXC::Slider const& slider, PGF::Rect const& bounds)
    {
        static thread_local winrt::Windows::UI::ViewManagement::AccessibilitySettings accessibility;
        auto foreground = meter.volumeFill.Fill().try_as<PGXM::SolidColorBrush>();
        if (!foreground) foreground = slider.Foreground().try_as<PGXM::SolidColorBrush>();
        auto accent = foreground ? foreground.Color() : AccentColor();
        const auto rail = meter.track.Fill().try_as<PGXM::SolidColorBrush>();
        auto gray = rail && rail.Color().A == 255 && rail.Opacity() == 1
            ? rail.Color() : PGUI::Color{ 255, 128, 128, 128 };
        if (accessibility.HighContrast())
        {
            static thread_local winrt::Windows::UI::ViewManagement::UISettings ui;
            accent = ui.UIElementColor(winrt::Windows::UI::ViewManagement::UIElementType::Hotlight);
            gray = ui.UIElementColor(winrt::Windows::UI::ViewManagement::UIElementType::GrayText);
        }
        accent.A = 255;
        gray.A = 255;

        const auto thumbCenter = meter.thumb.TransformToVisual(meter.overlay)
            .TransformPoint({ static_cast<float>(meter.thumb.ActualWidth() / 2.0), 0 });
        const auto split = std::clamp(static_cast<double>(thumbCenter.X - bounds.X) / bounds.Width, 0.0, 1.0);
        const PGF::Point end{ bounds.Width, 0 };
        if (meter.brush.EndPoint() != end) meter.brush.EndPoint(end);
        const auto stops = meter.brush.GradientStops();
        const auto leading = stops.GetAt(0);
        const auto accentEdge = stops.GetAt(1);
        const auto grayEdge = stops.GetAt(2);
        const auto trailing = stops.GetAt(3);
        if (leading.Color() != accent)
        {
            leading.Color(accent);
            accentEdge.Color(accent);
        }
        if (trailing.Color() != gray)
        {
            grayEdge.Color(gray);
            trailing.Color(gray);
        }
        if (accentEdge.Offset() != split)
        {
            accentEdge.Offset(split);
            grayEdge.Offset(split);
        }
    }

    inline StereoMeter MakeStereoMeter()
    {
        StereoMeter meter;
        meter.layer = PGXC::Grid();
        meter.layer.Width(kVolumeSliderWidth);
        meter.layer.Height(32);
        meter.layer.UseLayoutRounding(true);
        meter.overlay = PGXC::Canvas();
        meter.overlay.IsHitTestVisible(false);
        meter.brush = PGXM::LinearGradientBrush();
        meter.brush.MappingMode(PGXM::BrushMappingMode::Absolute);
        meter.brush.StartPoint({ 0, 0 });
        for (const double offset : { 0.0, 0.5, 0.5, 1.0 })
        {
            PGXM::GradientStop stop;
            stop.Offset(offset);
            meter.brush.GradientStops().Append(stop);
        }

        PGXM::PathGeometry geometry;
        geometry.FillRule(PGXM::FillRule::Nonzero);
        for (const auto& figure : { meter.left.figure, meter.right.figure })
            geometry.Figures().Append(figure);
        meter.shape = PGXSH::Path();
        meter.shape.Data(geometry);
        meter.shape.Fill(meter.brush);
        meter.shape.Stretch(PGXM::Stretch::None);
        meter.shape.IsHitTestVisible(false);
        meter.shape.Visibility(PGX::Visibility::Collapsed);
        meter.overlay.Children().Append(meter.shape);
        return meter;
    }

    inline PGX::FrameworkElement FindSliderPart(PGX::DependencyObject const& node, std::wstring_view name)
    {
        if (auto element = node.try_as<PGX::FrameworkElement>();
            element && std::wstring_view(element.Name()) == name) return element;
        const int count = PGXM::VisualTreeHelper::GetChildrenCount(node);
        for (int index = 0; index < count; ++index)
        {
            if (auto part = FindSliderPart(PGXM::VisualTreeHelper::GetChild(node, index), name)) return part;
        }
        return nullptr;
    }

    inline void MountStereoMeter(StereoMeter& meter, PGX::DependencyObject const& root)
    {
        if (auto previous = PGXM::VisualTreeHelper::GetParent(meter.overlay).try_as<PGXC::Grid>())
        {
            std::uint32_t index = 0;
            if (previous.Children().IndexOf(meter.overlay, index)) previous.Children().RemoveAt(index);
        }
        meter.templateRoot = root;
        meter.track = nullptr;
        meter.volumeFill = nullptr;
        meter.thumb = nullptr;
        if (!root) return;

        const auto track = FindSliderPart(root, L"HorizontalTrackRect").try_as<PGXSH::Shape>();
        const auto volumeFill = FindSliderPart(root, L"HorizontalDecreaseRect").try_as<PGXSH::Shape>();
        const auto thumb = FindSliderPart(root, L"HorizontalThumb");
        if (!track || !volumeFill || !thumb) return;
        const auto parent = PGXM::VisualTreeHelper::GetParent(track).try_as<PGXC::Grid>();
        if (!parent || PGXM::VisualTreeHelper::GetParent(volumeFill) != parent ||
            PGXM::VisualTreeHelper::GetParent(thumb) != parent) return;

        int highest = 0;
        for (const auto& child : parent.Children())
        {
            if (child != thumb) highest = std::max(highest, PGXC::Canvas::GetZIndex(child));
        }
        if (highest > std::numeric_limits<int>::max() - 3) return;

        PGXC::Grid::SetRowSpan(meter.overlay, static_cast<int>(std::max(1u, parent.RowDefinitions().Size())));
        PGXC::Grid::SetColumnSpan(meter.overlay, static_cast<int>(std::max(1u, parent.ColumnDefinitions().Size())));
        PGXC::Canvas::SetZIndex(meter.overlay, highest + 1);
        PGXC::Canvas::SetZIndex(volumeFill, highest + 2);
        PGXC::Canvas::SetZIndex(thumb, highest + 3);
        parent.Children().Append(meter.overlay);
        meter.track = track;
        meter.volumeFill = volumeFill;
        meter.thumb = thumb;
    }

    inline void UpdateStereoMeter(StereoMeter& meter, PGXC::Slider const& slider,
                                  MeterClock::duration elapsed)
    {
        const float leftPeak = meter.leftLevel.Advance(elapsed);
        const float rightPeak = meter.rightLevel.Advance(elapsed);
        const auto root = PGXM::VisualTreeHelper::GetChildrenCount(slider) != 0
            ? PGXM::VisualTreeHelper::GetChild(slider, 0) : nullptr;
        if (meter.templateRoot != root || !meter.track || !PGXM::VisualTreeHelper::GetParent(meter.track))
        {
            MountStereoMeter(meter, root);
        }
        if (!meter.track)
        {
            meter.shape.Visibility(PGX::Visibility::Collapsed);
            return;
        }
        for (const auto& part : { meter.track, meter.volumeFill })
        {
            if (const auto rectangle = part.try_as<PGXSH::Rectangle>())
            {
                const double radius = rectangle.ActualHeight() / 2.0;
                if (rectangle.RadiusX() != radius) rectangle.RadiusX(radius);
                if (rectangle.RadiusY() != radius) rectangle.RadiusY(radius);
            }
        }
        if ((leftPeak == 0 && rightPeak == 0) ||
            meter.track.ActualWidth() <= 0 || meter.track.ActualHeight() <= 0 || meter.thumb.ActualWidth() <= 0)
        {
            if (meter.shape.Visibility() != PGX::Visibility::Collapsed)
                meter.shape.Visibility(PGX::Visibility::Collapsed);
            return;
        }
        const auto bounds = meter.track.TransformToVisual(meter.overlay).TransformBounds({ 0, 0,
            static_cast<float>(meter.track.ActualWidth()), static_cast<float>(meter.track.ActualHeight()) });
        if (bounds.Width <= 0 || bounds.Height <= 0)
        {
            meter.shape.Visibility(PGX::Visibility::Collapsed);
            return;
        }
        const float channelHeight = bounds.Height * 0.875f;
        const float shoulder = channelHeight - bounds.Height / 2.0f;
        const auto leftWidth = bounds.Width * leftPeak;
        const auto rightWidth = bounds.Width * rightPeak;
        if (PGXC::Canvas::GetLeft(meter.shape) != bounds.X) PGXC::Canvas::SetLeft(meter.shape, bounds.X);
        if (PGXC::Canvas::GetTop(meter.shape) != bounds.Y - shoulder) PGXC::Canvas::SetTop(meter.shape, bounds.Y - shoulder);
        meter.left.Update({ 0, 0, leftWidth, channelHeight }, channelHeight, shoulder, 0, 0);
        meter.right.Update({ 0, channelHeight, rightWidth, channelHeight }, 0, 0, shoulder, channelHeight);
        UpdateMeterBrush(meter, slider, bounds);
        if (meter.shape.Visibility() != PGX::Visibility::Visible) meter.shape.Visibility(PGX::Visibility::Visible);
    }

    inline void SendSetDefault(bool render, const std::wstring& deviceId);
    winrt::fire_and_forget RefreshRecording(ContextPtr context, std::wstring action, std::wstring device = {});
    inline void RefreshFooter(ContextPtr const& context);
    inline void MountPage(ContextPtr const& context, int page, const presentation::DeviceSnapshot* snapshot = nullptr);
    inline void LoadData(ContextPtr const& context, const presentation::DeviceSnapshot* snapshot = nullptr);
    inline void SyncAppTargets(ContextPtr const& context);
    inline void ScheduleSettleSync(ContextPtr const& context);
    inline void RefreshEnvironmentNow(ContextPtr const& context);
    inline void ScheduleEnvironmentRefresh(ContextPtr const& context,
        std::chrono::milliseconds delay = std::chrono::milliseconds(16));
    [[nodiscard]] inline bool IsInLiveTree(PGX::DependencyObject const& node);

    inline void SetButtonLabel(PGXC::Button const& button, std::wstring_view key,
        const std::vector<std::wstring>& args = {})
    {
        const auto label = winrt::hstring(text::Embedded().ResolveFormat(key, args));
        PGXC::ToolTipService::SetToolTip(button, winrt::box_value(label));
        PGXAU::AutomationProperties::SetName(button, label);
    }

    struct EndpointIconOption final
    {
        const wchar_t* glyph;
        std::wstring_view label;
    };

    inline constexpr std::array<EndpointIconOption, static_cast<std::size_t>(audio::DeviceIcon::Count)> kEndpointIcons{{
        { nullptr, L"page.device.icon.recommended" },
        { L"\xE767", L"page.device.icon.volume" },
        { L"\xE7F4", L"page.device.icon.display" },
        { L"\xE7F5", L"page.device.icon.speakers" },
        { L"\xE7F6", L"page.device.icon.headphones" },
        { L"\xE95B", L"page.device.icon.headset" },
        { L"\xF4C0", L"page.device.icon.earbuds" },
        { L"\xE720", L"page.device.icon.microphone" },
        { L"\xE717", L"page.device.icon.handset" },
        { L"\xE8D6", L"page.device.icon.audio" },
        { L"\xE702", L"page.device.icon.bluetooth" },
        { L"\xE7F7", L"page.device.icon.laptop" },
        { L"\xE95D", L"page.device.icon.projector" },
        { L"\xE967", L"page.device.icon.console" },
        { L"\xF27C", L"page.device.icon.singleEarbud" },
        { L"\xF27F", L"page.device.icon.hearingAid" },
        { L"\xE88E", L"page.device.icon.usb" },
        { L"\xE952", L"page.device.icon.dock" },
        { L"\xE977", L"page.device.icon.desktop" },
        { L"\xE8EA", L"page.device.icon.mobile" },
        { L"\xF4C3", L"page.device.icon.mixer" },
        { L"\xE8AA", L"page.device.icon.conference" },
        { L"\xF117", L"page.device.icon.casting" },
        { L"\xE955", L"page.device.icon.mediaPlayer" },
        { L"\xE954", L"page.device.icon.mediaRecorder" },
        { L"\xE789", L"page.device.icon.megaphone" },
        { L"\xEC94", L"page.device.icon.vrHeadset" },
        { L"\xE722", L"page.device.icon.camera" },
        { L"\xF12E", L"page.device.icon.recordingMicrophone" },
        { L"\xE9E9", L"page.device.icon.equalizer" },
        { L"\xE958", L"page.device.icon.optical" },
        { L"\xE953", L"page.device.icon.networkAudio" },
        { L"\xE93E", L"page.device.icon.streaming" },
        { L"\xF539", L"page.device.icon.audioShare" },
        { L"\xEC4F", L"page.device.icon.music" },
        { L"\xE7F9", L"page.device.icon.externalMonitor" },
        { L"\xECF0", L"page.device.icon.wiredUsb" },
        { L"\xECF1", L"page.device.icon.wirelessUsb" },
        { L"\xED47", L"page.device.icon.mediaReceiver" },
        { L"\xE7F3", L"page.device.icon.displayAudio" }
    }};

    inline constexpr wchar_t kEndpointIconFont[] = L"Segoe Fluent Icons";
    inline constexpr double kEndpointIconEditOpacity = 0.13;

    inline void SetEndpointIconPointerBackgrounds(PGXC::Button const& button, double opacity)
    {
        const auto resources = button.Resources();
        resources.Insert(winrt::box_value(L"ButtonBackgroundPointerOver"), AccentBrush(opacity + 0.06));
        resources.Insert(winrt::box_value(L"ButtonBackgroundPressed"), AccentBrush(opacity + 0.12));
    }

    inline void SetEndpointIconEditing(PGXC::Button const& button, bool editing)
    {
        button.IsHitTestVisible(editing);
        button.IsTabStop(editing);
        button.Background(editing ? AccentBrush(kEndpointIconEditOpacity) : nullptr);
    }

    inline void CloseEndpointIconPicker(ContextPtr const& context)
    {
        const auto picker = std::exchange(context->endpointIconPicker, std::nullopt);
        if (!picker) return;
        const auto children = picker->row.Children();
        std::uint32_t index = 0;
        if (children.IndexOf(picker->view, index)) children.RemoveAt(index);
        picker->name.Visibility(PGX::Visibility::Visible);
        picker->controls.Visibility(PGX::Visibility::Visible);
    }

    inline void ToggleEndpointIconPicker(ContextPtr const& context, PGXC::Button const& anchor, PGXC::Grid const& row,
        PGXC::StackPanel const& name, PGXC::StackPanel const& controls,
        const std::wstring& deviceId, audio::DeviceIcon recommended)
    {
        const bool wasOpen = context->endpointIconPicker && context->endpointIconPicker->anchor == anchor;
        CloseEndpointIconPicker(context);
        if (wasOpen) return;

        PGXC::ScrollViewer picker;
        picker.HorizontalScrollMode(PGXC::ScrollMode::Enabled);
        picker.HorizontalScrollBarVisibility(PGXC::ScrollBarVisibility::Hidden);
        picker.VerticalScrollMode(PGXC::ScrollMode::Disabled);
        picker.VerticalScrollBarVisibility(PGXC::ScrollBarVisibility::Disabled);
        picker.VerticalAlignment(PGX::VerticalAlignment::Center);
        picker.Height(34);
        picker.IsTabStop(false);

        PGXC::StackPanel strip;
        strip.Orientation(PGXC::Orientation::Horizontal);
        const auto selected = settings::ReadDeviceIcon(deviceId);
        const auto accent = AccentColor();
        PGXM::SolidColorBrush selectedOutline{nullptr};
        const auto weak = std::weak_ptr<Context>(context);
        const auto weakAnchor = winrt::make_weak(anchor);
        for (std::size_t index = 0; index < kEndpointIcons.size(); ++index)
        {
            const auto choice = static_cast<audio::DeviceIcon>(index);
            const auto& visual = kEndpointIcons[static_cast<std::size_t>(audio::ResolveDeviceIcon(recommended, choice))];
            const auto button = BareButton(L"");
            button.MinWidth(0);
            button.Width(34);
            button.Height(32);
            button.Padding(PGX::ThicknessHelper::FromUniformLength(0));
            button.Margin(PGX::ThicknessHelper::FromLengths(0, 0, index == 0 ? 8 : 2, 0));
            const auto glyph = Glyph(visual.glyph, 16, kEndpointIconFont);
            glyph.Margin(PGX::ThicknessHelper::FromUniformLength(0));
            button.Content(glyph);
            const auto outline = PGXM::SolidColorBrush(accent);
            outline.Opacity(choice == selected ? 1.0 : 0.0);
            button.BorderBrush(outline);
            button.BorderThickness(PGX::ThicknessHelper::FromUniformLength(1));
            button.Resources().Insert(winrt::box_value(L"ButtonBorderBrushPointerOver"), outline);
            button.Resources().Insert(winrt::box_value(L"ButtonBorderBrushPressed"), outline);
            if (choice == selected) selectedOutline = outline;
            if (choice == audio::DeviceIcon::Recommended)
            {
                constexpr double recommendedOpacity = 0.08;
                button.Background(AccentBrush(recommendedOpacity));
                SetEndpointIconPointerBackgrounds(button, recommendedOpacity);
                SetButtonLabel(button, kEndpointIcons[index].label, { text::Embedded().Resolve(visual.label) });
            }
            else
                SetButtonLabel(button, visual.label);
            const auto label = PGXAU::AutomationProperties::GetName(button);
            button.Click([weak, weakAnchor, deviceId, recommended, choice, outline, label]
                (PGF::IInspectable const& sender, auto const&)
            {
                const auto live = weak.lock();
                const auto icon = weakAnchor.get();
                if (!live || !live->active || !live->editing || !icon ||
                    !live->endpointIconPicker || live->endpointIconPicker->anchor != icon) return;
                const auto option = sender.as<PGXC::Button>();
                if (!settings::WriteDeviceIcon(deviceId, choice))
                {
                    SetButtonLabel(option, L"page.device.icon.saveFailed");
                    return;
                }
                PGXC::ToolTipService::SetToolTip(option, winrt::box_value(label));
                PGXAU::AutomationProperties::SetName(option, label);
                const auto visual = audio::ResolveDeviceIcon(recommended, choice);
                icon.Content().as<PGXC::TextBlock>().Text(kEndpointIcons[static_cast<std::size_t>(visual)].glyph);
                auto& current = live->endpointIconPicker->selectedOutline;
                if (current != outline)
                {
                    current.Opacity(0);
                    outline.Opacity(1);
                    current = outline;
                }
            });
            strip.Children().Append(button);
        }
        strip.PointerWheelChanged([weakPicker = winrt::make_weak(picker)](auto const&, PGXIN::PointerRoutedEventArgs const& args)
        {
            const auto view = weakPicker.get();
            const auto wheel = args.GetCurrentPoint(nullptr).Properties();
            if (!view || wheel.IsHorizontalMouseWheel() || view.ScrollableWidth() <= 0) return;
            const double offset = view.HorizontalOffset() - 102.0 * wheel.MouseWheelDelta() / WHEEL_DELTA;
            view.ChangeView(std::clamp(offset, 0.0, view.ScrollableWidth()), nullptr, nullptr, true);
            args.Handled(true);
        });
        picker.Content(strip);
        PGXC::Grid::SetColumn(picker, 1);
        PGXC::Grid::SetColumnSpan(picker, 2);
        name.Visibility(PGX::Visibility::Collapsed);
        controls.Visibility(PGX::Visibility::Collapsed);
        row.Children().Append(picker);
        context->endpointIconPicker.emplace(EndpointIconPicker{ anchor, row, name, controls, picker, selectedOutline });
    }

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
        const auto choice = audio::ResolveDeviceIcon(device.recommendedIcon, settings::ReadDeviceIcon(device.id));
        const auto glyph = Glyph(kEndpointIcons[static_cast<std::size_t>(choice)].glyph, 14, kEndpointIconFont);
        glyph.Margin(PGX::ThicknessHelper::FromUniformLength(0));
        const auto icon = BareButton(L"");
        icon.MinWidth(0);
        icon.Width(kAudioRowIconWidth);
        icon.Height(28);
        icon.HorizontalContentAlignment(PGX::HorizontalAlignment::Center);
        icon.Margin(PGX::ThicknessHelper::FromLengths(0, 0, kAudioRowIconGap, 0));
        icon.Padding(PGX::ThicknessHelper::FromUniformLength(0));
        icon.Content(glyph);
        SetEndpointIconPointerBackgrounds(icon, kEndpointIconEditOpacity);
        SetEndpointIconEditing(icon, context->editing);
        SetButtonLabel(icon, L"page.device.icon.change");
        context->endpointIcons.push_back(icon);
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
            if (!weakContext->active) return;
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
            if (!weakContext->active || synchronizing->load(std::memory_order_acquire)) return;
            if (!weakContext->manager) return;
            if (auto s = sender.try_as<PGXC::Slider>())
            {
                weakContext->manager->SetVolume(deviceId, static_cast<float>(s.Value() / 100.0));
            }
        });
        auto refreshAfterDrag = [weakContext, deviceId, slider, dragging, synchronizing]()
        {
            if (!weakContext->active) return;
            dragging->store(false, std::memory_order_release);
            audio::EndpointVolume current;
            if (!weakContext->manager || !weakContext->manager->GetVolume(deviceId, current).IsOk()) return;
            synchronizing->store(true, std::memory_order_release);
            slider.Value(current.level * 100.0);
            synchronizing->store(false, std::memory_order_release);
        };
        ObserveSliderDrag(slider, dragging, refreshAfterDrag);
        StereoMeter meter = MakeStereoMeter();
        meter.layer.Children().Append(slider);
        trailing.Children().Append(meter.layer);

        context->endpointMeters.push_back(std::make_shared<MeterUpdate>(
            [weak = std::weak_ptr<Context>(context), deviceId, meter, slider](const presentation::MeterFrame& frame) mutable
        {
            const auto live = weak.lock();
            if (!live || !live->active || !slider.IsLoaded()) return;
            if (frame.sample)
            {
                float left = 0.0f;
                float right = 0.0f;
                if (live->manager) live->manager->GetPeak(deviceId, left, right);
                meter.Sample(left, right);
            }
            if (live->active) UpdateStereoMeter(meter, slider, frame.elapsed);
        }));

        auto dispatcher = slider.Dispatcher();
        if (dispatcher)
        {
            auto monitor = audio::detail::EndpointVolumeMonitor::Create(
                deviceId, [weakSlider = winrt::make_weak(slider), weakMute = winrt::make_weak(mute),
                           weak = std::weak_ptr<Context>(context), dispatcher, dragging, synchronizing](float externalLevel, bool externalMuted)
                {
                    try
                    {
                        dispatcher.RunAsync(
                            winrt::Windows::UI::Core::CoreDispatcherPriority::Low,
                            [weakSlider, weakMute, weak, dragging, synchronizing, externalLevel, externalMuted]()
                            {
                                try
                                {
                                    const auto live = weak.lock();
                                    const auto slider = weakSlider.get();
                                    const auto mute = weakMute.get();
                                    if (!live || !live->active || !slider || !mute || !slider.IsLoaded() ||
                                        dragging->load(std::memory_order_acquire)) return;
                                    synchronizing->store(true, std::memory_order_release);
                                    struct Reset final
                                    {
                                        std::shared_ptr<std::atomic_bool> value;
                                        ~Reset() { value->store(false, std::memory_order_release); }
                                    } reset{ synchronizing };
                                    slider.Value(externalLevel * 100.0);
                                    if (live->active) SetMuteGlyph(mute, externalMuted);
                                }
                                catch (...) { Log(L"Footer retry dispatch failed"); }
                            });
                    }
                    catch (const winrt::hresult_error&)
                    {
                        Log(L"Footer retry dispatch failed");
                    }
                });
            if (monitor) context->endpointMonitors.push_back(std::move(monitor));
        }

        PGXC::Grid::SetColumn(trailing, 2);
        grid.Children().Append(trailing);
        icon.Click([weak = std::weak_ptr<Context>(context), weakRow = winrt::make_weak(grid),
            weakName = winrt::make_weak(block), weakControls = winrt::make_weak(trailing), deviceId,
            recommended = device.recommendedIcon](PGF::IInspectable const& sender, auto const&)
        {
            const auto live = weak.lock();
            const auto row = weakRow.get();
            const auto name = weakName.get();
            const auto controls = weakControls.get();
            if (!live || !live->active || !live->editing || Settings().page != 0 || !row || !name || !controls) return;
            ToggleEndpointIconPicker(live, sender.as<PGXC::Button>(), row, name, controls, deviceId, recommended);
        });
        return grid;
    }

    inline void SendSetDefault(bool render, const std::wstring& deviceId);

    inline void QueueRecordingChange(ContextPtr const& context, bool enabled)
    {
        context->pendingRecording.Queue(enabled, ::GetTickCount64());
        RefreshFooter(context);
        if (!context->recordingActionTimer)
        {
            context->recordingActionTimer = PGX::DispatcherTimer();
            context->recordingActionTimer.Interval(std::chrono::milliseconds(200));
            const std::weak_ptr<Context> weak = context;
            context->recordingActionTimer.Tick([weak](auto const&, auto const&)
            {
                const auto live = weak.lock();
                if (!live) return;
                if (!live->active || Settings().page != 0 || !IsInLiveTree(live->list))
                {
                    live->recordingActionTimer.Stop();
                    live->pendingRecording.Reset();
                    return;
                }
                if (live->recordingBusy || !live->pendingRecording.Due(::GetTickCount64())) return;
                live->recordingActionTimer.Stop();
                const auto desired = live->pendingRecording.Take(::GetTickCount64());
                if (desired && *desired != Settings().recordingMode)
                    RefreshRecording(live, *desired ? L"on" : L"off");
            });
        }
        context->recordingActionTimer.Stop();
        context->recordingActionTimer.Start();
    }

    inline void QueueDeviceSelection(ContextPtr const& context, bool render, const std::wstring& deviceId)
    {
        (render ? context->pendingRenderSelection : context->pendingCaptureSelection).Queue(
            deviceId, ::GetTickCount64(), presentation::kDeviceSelectionDelayMs);
        if (!context->deviceSelectionTimer)
        {
            context->deviceSelectionTimer = PGX::DispatcherTimer();
            context->deviceSelectionTimer.Interval(std::chrono::milliseconds(presentation::kDeviceSelectionDelayMs));
            const std::weak_ptr<Context> weak = context;
            context->deviceSelectionTimer.Tick([weak](auto const&, auto const&)
            {
                const auto live = weak.lock();
                if (!live) return;
                if (!live->active || Settings().page != 0 || !IsInLiveTree(live->list))
                {
                    live->deviceSelectionTimer.Stop();
                    live->pendingRenderSelection.Reset();
                    live->pendingCaptureSelection.Reset();
                    return;
                }
                if (live->recordingBusy || live->pendingRecording) return;
                const auto now = ::GetTickCount64();
                if ((live->pendingRenderSelection && !live->pendingRenderSelection.Due(now)) ||
                    (live->pendingCaptureSelection && !live->pendingCaptureSelection.Due(now))) return;
                live->deviceSelectionTimer.Stop();
                const auto output = live->pendingRenderSelection.Take(now).value_or(std::wstring());
                const auto input = live->pendingCaptureSelection.Take(now).value_or(std::wstring());
                if (!input.empty() && input != live->data->defaultCapture) SendSetDefault(false, input);
                if (!output.empty())
                {
                    if (Settings().recordingMode)
                    {
                        if (output != Settings().listenEndpoint) RefreshRecording(live, L"monitor", output);
                    }
                    else if (output != live->data->defaultRender) SendSetDefault(true, output);
                }
                ScheduleEnvironmentRefresh(live);
            });
        }
        context->deviceSelectionTimer.Stop();
        context->deviceSelectionTimer.Start();
    }

    inline void UpdateEndpointRows(PGXC::ListView const& list, const std::vector<audio::DeviceInfo>& devices)
    {
        if (!list) return;
        const auto items = list.Items();
        int selected = -1;
        bool moved = false;
        for (std::uint32_t position = 0; position < devices.size(); ++position)
        {
            const auto& device = devices[position];
            if (device.isDefault && selected < 0) selected = static_cast<int>(position);
            for (std::uint32_t index = position; index < items.Size(); ++index)
            {
                const auto item = items.GetAt(index).as<PGXC::ListViewItem>();
                if (device.id != std::wstring_view(winrt::unbox_value<winrt::hstring>(item.Tag()))) continue;
                if (index != position)
                {
                    items.RemoveAt(index);
                    items.InsertAt(position, item);
                    moved = true;
                }
                break;
            }
        }
        if (list.SelectedIndex() != selected) list.SelectedIndex(selected);
        if (moved) Log(L"Device rows reordered");
    }

    inline PGXC::ListView MakeEndpointList(ContextPtr const& context,
                                            const std::vector<audio::DeviceInfo>& devices,
                                            bool render)
    {
        PGXC::ListView list;
        PGXA::TransitionCollection transitions;
        PGXA::RepositionThemeTransition reposition;
        reposition.IsStaggeringEnabled(false);
        transitions.Append(reposition);
        transitions.Append(PGXA::ReorderThemeTransition());
        list.ItemContainerTransitions(transitions);
        list.Transitions(PGXA::TransitionCollection());
        list.SelectionMode(PGXC::ListViewSelectionMode::Single);
        list.IsItemClickEnabled(false);
        list.Padding(PGX::ThicknessHelper::FromLengths(0, 0, 0, 0));
        list.BorderThickness(PGX::ThicknessHelper::FromLengths(0, 0, 0, 0));
        list.Background(nullptr);

        for (const auto& device : devices)
        {
            PGXC::ListViewItem row;
            row.Padding(PGX::ThicknessHelper::FromLengths(kAudioRowLeadingInset, 0, kAudioRowTrailingInset, 0));
            row.HorizontalContentAlignment(PGX::HorizontalAlignment::Stretch);
            row.Tag(winrt::box_value(winrt::hstring(device.id)));
            row.Content(MakeEndpointRow(context, device));
            list.Items().Append(row);
        }

        UpdateEndpointRows(list, devices);
        list.SelectionChanged([weak = std::weak_ptr<Context>(context), render](PGF::IInspectable const& sender,
            PGXC::SelectionChangedEventArgs const&)
        {
            const auto live = weak.lock();
            if (!live || !live->active || live->updatingPage) return;
            const auto self = sender.as<PGXC::ListView>();
            if (const auto item = self.SelectedItem().try_as<PGXC::ListViewItem>())
                QueueDeviceSelection(live, render, std::wstring(winrt::unbox_value<winrt::hstring>(item.Tag())));
        });
        return list;
    }

    inline PGXA::Storyboard MakeHeightAnimation(PGX::FrameworkElement const& element, double from, double to)
    {
        static thread_local winrt::Windows::UI::ViewManagement::UISettings ui;
        if (!ui.AnimationsEnabled() || std::abs(from - to) < 0.5) return nullptr;

        PGXA::DoubleAnimation height;
        height.From(from);
        height.To(to);
        height.Duration(PGX::DurationHelper::FromTimeSpan(std::chrono::milliseconds(180)));
        height.EnableDependentAnimation(true);
        PGXA::CubicEase ease;
        ease.EasingMode(PGXA::EasingMode::EaseOut);
        height.EasingFunction(ease);
        PGXA::Storyboard::SetTarget(height, element);
        PGXA::Storyboard::SetTargetProperty(height, L"Height");

        PGXA::Storyboard storyboard;
        storyboard.Children().Append(height);
        return storyboard;
    }

    inline void AnimateInputDevices(PGXC::Canvas const& viewport, PGXC::ListView const& rows,
        const std::shared_ptr<PGXA::Storyboard>& active, bool expanded)
    {
        const double from = viewport.ActualHeight();
        const double to = expanded ? rows.ActualHeight() : 0.0;
        viewport.Height(from);
        if (const auto previous = std::exchange(*active, nullptr)) previous.Stop();

        const auto finish = [viewRef = winrt::make_weak(viewport), rowsRef = winrt::make_weak(rows), expanded]
        {
            const auto view = viewRef.get();
            const auto content = rowsRef.get();
            if (!view || !content) return;
            view.Height(expanded ? content.ActualHeight() : 0.0);
            view.IsHitTestVisible(expanded);
            content.IsEnabled(expanded);
        };

        Log(L"Input device fold requested expanded={0} from={1} to={2}",
            { expanded ? L"1" : L"0", std::to_wstring(from), std::to_wstring(to) });
        try
        {
            const auto storyboard = MakeHeightAnimation(viewport, from, to);
            if (!storyboard)
            {
                finish();
                return;
            }
            viewport.IsHitTestVisible(false);
            if (expanded) rows.IsEnabled(true);

            storyboard.Completed([weak = std::weak_ptr<PGXA::Storyboard>(active), finish, expanded]
                (PGF::IInspectable const& sender, PGF::IInspectable const&)
            {
                if (const auto current = weak.lock(); current && *current == sender.try_as<PGXA::Storyboard>())
                {
                    const auto completed = std::exchange(*current, nullptr);
                    finish();
                    completed.Stop();
                    Log(L"Input device fold completed expanded={0}", { expanded ? L"1" : L"0" });
                }
            });
            *active = storyboard;
            storyboard.Begin();
        }
        catch (const winrt::hresult_error& error)
        {
            *active = nullptr;
            finish();
            Log(L"Input device animation failed detail={0}", { std::wstring(error.message()) });
        }
    }

    inline PGXC::StackPanel BuildVolumeLayer(ContextPtr const& context)
    {
        PGXC::StackPanel layer;
        context->volumeLabel = SectionLabel(winrt::hstring(text::Embedded().Resolve(
            Settings().recordingMode ? L"page.label.listenOutput" : L"page.section.volume")));
        layer.Children().Append(context->volumeLabel);

        context->renderRows = MakeEndpointList(context, context->data->render, true);
        layer.Children().Append(context->renderRows);

        context->captureRows = nullptr;
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

        const auto rows = MakeEndpointList(context, context->data->capture, false);
        context->captureRows = rows;
        rows.Width(0.0);
        rows.IsEnabled(Settings().inputExpanded);

        PGXC::Canvas viewport;
        viewport.Height(0.0);
        viewport.IsHitTestVisible(Settings().inputExpanded);
        PGXM::RectangleGeometry clip;
        viewport.Clip(clip);
        viewport.SizeChanged([clip, rowsRef = winrt::make_weak(rows)]
            (auto const&, PGX::SizeChangedEventArgs const& args)
        {
            const auto size = args.NewSize();
            if (const auto content = rowsRef.get(); content && content.Width() != size.Width)
                content.Width(size.Width);
            clip.Rect({ 0.0f, 0.0f, size.Width, size.Height });
        });
        const auto active = std::make_shared<PGXA::Storyboard>(nullptr);
        rows.SizeChanged([viewRef = winrt::make_weak(viewport), weak = std::weak_ptr<PGXA::Storyboard>(active)]
            (auto const&, PGX::SizeChangedEventArgs const& args)
        {
            const auto animation = weak.lock();
            const auto view = viewRef.get();
            if (view && animation && !*animation && Settings().inputExpanded)
                view.Height(args.NewSize().Height);
        });
        viewport.Children().Append(rows);

        PGXC::StackPanel group;
        group.Children().Append(head);
        group.Children().Append(viewport);
        head.Tapped([arrow, viewport, rows, active]
            (PGF::IInspectable const&, PGX::Input::TappedRoutedEventArgs const&)
        {
            const bool expanded = !Settings().inputExpanded;
            Settings().inputExpanded = expanded;
            arrow.Text(expanded ? L"\xE70E" : L"\xE70D");
            AnimateInputDevices(viewport, rows, active, expanded);
        });

        layer.Children().Append(group);
        return layer;
    }

    inline bool SendPipeLine(const std::string& line) { return TapPageSendPipe(line.c_str()) != 0; }

    inline bool SendOrLog(const std::string& line, std::wstring_view verb)
    {
        if (!SendPipeLine(line))
        {
            Log(L"Pipe command dropped verb={0}", { std::wstring(verb) });
            return false;
        }
        return true;
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

    [[nodiscard]] inline PGX::FrameworkElement MakeAppIcon(const audio::AppSessionInfo& session)
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

            PGXC::Image image;
            image.Source(source);
            image.Width(20);
            image.Height(20);
            image.Stretch(PGXM::Stretch::Uniform);
            image.VerticalAlignment(PGX::VerticalAlignment::Center);
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

        for (const auto& device : context->data->routingRender)
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
        Log(L"Application route app={0} target={1}", { appKey, std::wstring(line.name.Text()) });
        return line;
    }

    [[nodiscard]] inline PGXC::ComboBox MakeRedirectCombo(ContextPtr const& context, const audio::AppSessionInfo& session,
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

        for (std::size_t i = 0; i < context->data->routingRender.size(); ++i)
        {
            const auto& device = context->data->routingRender[i];
            PGXC::ComboBoxItem item;
            item.Content(winrt::box_value(winrt::hstring(audio::DisplayDeviceName(device, Settings().showDriverName))));
            item.Tag(winrt::box_value(winrt::hstring(device.id)));
            combo.Items().Append(item);

            if (device.id == current)
            {
                combo.SelectedIndex(static_cast<int>(i) + 1);
            }
        }

        const std::vector<audio::DeviceInfo> snapshot = context->data->routingRender;
        const std::uint32_t processId = session.processId;
        const std::wstring appKey = session.appKey;
        combo.DropDownOpened([appKey](PGF::IInspectable const&, PGF::IInspectable const&)
        {
            Log(L"Application route menu opened app={0}", { appKey });
        });
        combo.DropDownClosed([appKey](PGF::IInspectable const&, PGF::IInspectable const&)
        {
            Log(L"Application route menu closed app={0}", { appKey });
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
                    Log(L"Application route selected pid={0} target={1}", { std::to_wstring(processId), L"default" });
                    SendSetRedirect(processId, std::wstring());
                    ScheduleSettleSync(context);
                    onPicked(std::wstring());
                    return;
                }

                const std::size_t position = static_cast<std::size_t>(index) - 1;
                if (position < snapshot.size())
                {
                    context->redirects[appKey] = snapshot[position].id;
                    Log(L"Application route selected pid={0} target={1}", { std::to_wstring(processId), snapshot[position].friendlyName });
                    SendSetRedirect(processId, snapshot[position].id);
                    ScheduleSettleSync(context);
                    onPicked(audio::DisplayDeviceName(snapshot[position], Settings().showDriverName));
                }
            }
        });
        return combo;
    }

    inline void SetVisibilityButtonState(PGXC::Button const& button, bool hidden)
    {
        PGXC::Border content;
        content.Width(32);
        content.Height(28);
        content.CornerRadius(PGX::CornerRadiusHelper::FromUniformRadius(5));
        if (hidden)
        {
            const auto fill = AccentBrush();
            fill.Opacity(0.25);
            content.Background(fill);
        }
        const auto eye = Glyph(hidden ? L"\xED1A" : L"\xE7B3", 20);
        eye.Margin(PGX::ThicknessHelper::FromUniformLength(0));
        eye.HorizontalAlignment(PGX::HorizontalAlignment::Center);
        content.Child(eye);
        button.Content(content);
        SetButtonLabel(button, hidden ? L"page.app.unhide" : L"page.app.hide");
    }

    inline PGXC::Button MakeVisibilityButton(ContextPtr const& context, const audio::AppSessionInfo& session, bool hidden)
    {
        const auto button = BareButton(L"");
        button.MinWidth(0);
        button.Padding(PGX::ThicknessHelper::FromUniformLength(0));
        SetVisibilityButtonState(button, hidden);
        button.Click([weak = std::weak_ptr<Context>(context), appKey = session.appKey, instanceId = session.instanceId]
            (PGF::IInspectable const& sender, PGX::RoutedEventArgs const&)
        {
            const auto live = weak.lock();
            if (!live || !live->active || !live->editing || Settings().page != 0) return;
            const auto button = sender.as<PGXC::Button>();
            const bool hidden = !settings::IsAudioSessionHidden(appKey, instanceId);
            if (settings::SetAudioSessionHidden(appKey, instanceId, hidden))
                SetVisibilityButtonState(button, hidden);
            else
                SetButtonLabel(button, L"page.app.visibilityFailed");
        });
        return button;
    }

    inline PGXC::StackPanel MakeAppRow(ContextPtr const& context, const audio::AppSessionInfo& session,
                                      const std::shared_ptr<audio::IAudioSessionHandle>& handle, bool hidden)
    {
        const std::wstring appKey = session.appKey;
        const bool movable = session.processId != 0 && !appKey.empty() && appKey != L"#system";
        PGXC::Grid grid = Row(44);
        grid.Margin(PGX::ThicknessHelper::FromLengths(0, 0, kAudioRowTrailingInset, 0));

        PGXC::Grid titleBand;
        PGXC::ColumnDefinition leading;
        PGXC::ColumnDefinition flexible;
        leading.Width(PGX::GridLengthHelper::FromValueAndType(kAudioRowIconWidth + kAudioRowIconGap, PGX::GridUnitType::Pixel));
        flexible.Width(PGX::GridLengthHelper::FromValueAndType(1, PGX::GridUnitType::Star));
        titleBand.ColumnDefinitions().Append(leading);
        titleBand.ColumnDefinitions().Append(flexible);

        PGX::FrameworkElement icon = MakeAppIcon(session);
        icon.HorizontalAlignment(PGX::HorizontalAlignment::Center);
        icon.Margin(PGX::ThicknessHelper::FromLengths(0, 0, kAudioRowIconGap, 0));
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
        if (movable && !context->editing)
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

        auto binding = std::make_shared<AppRowBinding>();
        binding->handle = handle;
        binding->appKey = appKey;

        PGXC::Button mute = MakeMuteButton(session.muted);
        binding->mute = mute;
        if (handle)
        {
            mute.Click([binding, mute, appKey](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
            {
                const auto handle = binding->handle;
                if (!binding->active || !handle) return;
                float level = 0.0f;
                bool muted = false;
                if (!handle->GetState(level, muted).IsOk() || !binding->active) return;
                if (handle->SetMuted(!muted).IsOk()) SetMuteGlyph(mute, !muted);
                Log(L"Application mute changed app={0} action={1}", { appKey, muted ? L"unmute" : L"mute" });
            });
        }
        else
        {
            mute.IsEnabled(false);
        }

        PGXC::Slider slider = MakeVolumeSlider(session.volume);
        binding->slider = slider;
        if (handle)
        {
            slider.ValueChanged([binding, appKey](PGF::IInspectable const& sender, PGXCP::RangeBaseValueChangedEventArgs const&)
            {
                const auto handle = binding->handle;
                if (!binding->active || !handle || binding->synchronizing->load(std::memory_order_acquire)) return;
                if (auto s = sender.try_as<PGXC::Slider>())
                {
                    handle->SetVolume(static_cast<float>(s.Value() / 100.0));
                    Log(L"Application volume changed app={0} percent={1}", { appKey, std::to_wstring(static_cast<int>(s.Value())) });
                }
            });

            auto refreshAfterDrag = [binding]()
            {
                binding->dragging->store(false, std::memory_order_release);
                const auto handle = binding->handle;
                if (!binding->active || !handle) return;

                float level = 0.0f;
                bool muted = false;
                if (!handle->GetState(level, muted).IsOk() || !binding->active) return;

                binding->synchronizing->store(true, std::memory_order_release);
                binding->slider.Value(level * 100.0);
                binding->synchronizing->store(false, std::memory_order_release);
            };
            ObserveSliderDrag(slider, binding->dragging, refreshAfterDrag);
        }
        else
        {
            slider.IsEnabled(false);
        }
        context->rows.push_back(binding);
        StereoMeter meter = MakeStereoMeter();
        meter.layer.Children().Append(slider);

        context->appMeters.push_back(std::make_shared<MeterUpdate>(
            [binding, meter, slider, nextStatePoll = MeterClock::now() + std::chrono::milliseconds(200)]
            (const presentation::MeterFrame& frame) mutable
        {
            const auto handle = binding->handle;
            if (!binding->active || !slider.IsLoaded()) return;

            if (handle && frame.now >= nextStatePoll && !binding->dragging->load(std::memory_order_acquire))
            {
                nextStatePoll = frame.now + std::chrono::milliseconds(200);
                float level = 0.0f;
                bool muted = false;
                if (handle->GetState(level, muted).IsOk() && binding->active)
                {
                    binding->synchronizing->store(true, std::memory_order_release);
                    slider.Value(level * 100.0);
                    if (binding->active) SetMuteGlyph(binding->mute, muted);
                    binding->synchronizing->store(false, std::memory_order_release);
                }
            }

            if (frame.sample && binding->active)
            {
                float left = 0.0f;
                float right = 0.0f;
                if (handle) handle->GetPeak(left, right);
                meter.Sample(left, right);
            }
            if (binding->active) UpdateStereoMeter(meter, slider, frame.elapsed);
        }));

        PGXC::StackPanel volumeHost;
        volumeHost.Orientation(PGXC::Orientation::Horizontal);
        volumeHost.VerticalAlignment(PGX::VerticalAlignment::Center);
        volumeHost.Children().Append(mute);
        volumeHost.Children().Append(meter.layer);

        PGXC::Grid volumeHostGrid;
        volumeHostGrid.Children().Append(volumeHost);

        if (context->editing)
        {
            trailing.Children().Append(MakeVisibilityButton(context, session, hidden));
            trailing.Children().Append(volumeHostGrid);
        }
        else if (movable)
        {
            PGXCP::ToggleButton routeButton;
            const auto comboRef = std::make_shared<PGXC::ComboBox>(nullptr);
            PGXC::TextBlock glyph = Text(winrt::hstring(L"\xE724"), 14);
            glyph.FontFamily(PGXM::FontFamily(L"Segoe MDL2 Assets"));
            routeButton.Content(glyph);
            routeButton.MinHeight(0);
            routeButton.Padding(PGX::ThicknessHelper::FromLengths(8, 4, 8, 4));
            routeButton.Background(nullptr);
            routeButton.BorderThickness(PGX::ThicknessHelper::FromLengths(0, 0, 0, 0));

            PGXC::Grid redirectPanel;
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
                Log(L"Application list expanded");
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
                Log(L"Application list collapsed");
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

    inline std::wstring AppRowKey(PGXC::Border const& row)
    {
        return std::wstring(winrt::unbox_value<winrt::hstring>(row.Tag()));
    }

    inline void StopAppRowMotions(ContextPtr const& context)
    {
        const auto motions = std::exchange(context->appRowMotions, {});
        for (const auto& [key, motion] : motions) motion.animation.Stop();
    }

    inline void AnimateAppRow(ContextPtr const& context, PGXC::Border const& row,
        const std::wstring& key, bool removing)
    {
        const double from = row.IsLoaded() ? row.ActualHeight() : 0.0;
        row.Height(from);
        if (const auto found = context->appRowMotions.find(key); found != context->appRowMotions.end())
        {
            const auto previous = found->second.animation;
            context->appRowMotions.erase(found);
            previous.Stop();
        }

        const auto finish = [panelRef = winrt::make_weak(context->appRows), rowRef = winrt::make_weak(row), removing]
        {
            const auto item = rowRef.get();
            if (!item) return;
            if (removing)
            {
                item.Height(0.0);
                if (const auto panel = panelRef.get())
                {
                    const auto children = panel.Children();
                    std::uint32_t index = 0;
                    if (children.IndexOf(item, index)) children.RemoveAt(index);
                }
            }
            else
            {
                item.Height(std::numeric_limits<double>::quiet_NaN());
                item.Clip(nullptr);
                item.IsHitTestVisible(true);
            }
        };

        try
        {
            const double width = context->appRows.ActualWidth();
            if (width <= 0.0)
            {
                finish();
                return;
            }
            double to = 0.0;
            if (!removing)
            {
                const auto content = row.Child();
                content.Measure({ static_cast<float>(width), std::numeric_limits<float>::infinity() });
                to = content.DesiredSize().Height;
            }
            const auto storyboard = MakeHeightAnimation(row, from, to);
            if (!storyboard)
            {
                finish();
                return;
            }

            row.IsHitTestVisible(false);
            PGXM::RectangleGeometry clip;
            clip.Rect({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(from) });
            row.Clip(clip);
            storyboard.Completed([weak = std::weak_ptr<Context>(context), key, finish]
                (PGF::IInspectable const& sender, PGF::IInspectable const&)
            {
                const auto live = weak.lock();
                if (!live) return;
                const auto found = live->appRowMotions.find(key);
                if (found == live->appRowMotions.end() || found->second.animation != sender.try_as<PGXA::Storyboard>()) return;
                const auto completed = found->second.animation;
                live->appRowMotions.erase(found);
                finish();
                completed.Stop();
            });
            context->appRowMotions.emplace(key, AppRowMotion{ storyboard, removing });
            storyboard.Begin();
        }
        catch (const winrt::hresult_error& error)
        {
            context->appRowMotions.erase(key);
            finish();
            Log(L"Mixer row animation failed detail={0}", { std::wstring(error.message()) });
        }
    }

    inline void UpdateAppRows(ContextPtr const& context)
    {
        std::vector<std::pair<std::wstring, PGX::UIElement>> desired;
        desired.reserve(context->data->apps.size());
        for (std::size_t i = 0; i < context->data->apps.size(); ++i)
        {
            const auto& app = context->data->apps[i];
            const bool hidden = settings::IsAudioSessionHidden(app.appKey, app.instanceId);
            if (hidden && !context->editing) continue;
            const auto handle = (i < context->data->handles.size()) ? context->data->handles[i] : nullptr;
            desired.emplace_back(app.appKey, MakeAppRow(context, app, handle, hidden));
        }
        if (desired.empty())
        {
            const bool empty = context->data->apps.empty();
            PGXC::TextBlock none = Text(winrt::hstring(text::Embedded().Resolve(
                empty ? L"page.none.apps" : L"page.none.visibleApps")), 12);
            none.Opacity(0.5);
            none.Margin(PGX::ThicknessHelper::FromLengths(8, 2, 4, 8));
            desired.emplace_back(std::wstring(), none);
            if (empty) Log(L"Application session list empty endpoint={0}", { context->data->appsEndpoint });
        }

        const auto children = context->appRows.Children();
        std::map<std::wstring, PGXC::Border> existing;
        std::uint32_t removed = 0;
        for (std::uint32_t index = children.Size(); index != 0; --index)
        {
            const auto row = children.GetAt(index - 1).as<PGXC::Border>();
            const auto key = AppRowKey(row);
            if (std::none_of(desired.begin(), desired.end(), [&](const auto& item) { return item.first == key; }))
            {
                const auto motion = context->appRowMotions.find(key);
                if (motion == context->appRowMotions.end() || !motion->second.removing)
                {
                    AnimateAppRow(context, row, key, true);
                    ++removed;
                }
            }
            else existing.emplace(key, row);
        }

        std::uint32_t position = 0;
        std::uint32_t added = 0;
        std::uint32_t moved = 0;
        for (const auto& [key, content] : desired)
        {
            while (position < children.Size())
            {
                const auto current = AppRowKey(children.GetAt(position).as<PGXC::Border>());
                const auto motion = context->appRowMotions.find(current);
                if (motion == context->appRowMotions.end() || !motion->second.removing || existing.contains(current)) break;
                ++position;
            }
            content.as<PGX::FrameworkElement>().VerticalAlignment(PGX::VerticalAlignment::Top);
            if (const auto found = existing.find(key); found != existing.end())
            {
                const auto& row = found->second;
                row.Child(content);
                std::uint32_t index = 0;
                if (children.IndexOf(row, index) && index != position)
                {
                    children.RemoveAt(index);
                    children.InsertAt(position, row);
                    ++moved;
                }
                const auto motion = context->appRowMotions.find(key);
                if (motion != context->appRowMotions.end() && motion->second.removing)
                    AnimateAppRow(context, row, key, false);
            }
            else
            {
                PGXC::Border row;
                row.Tag(winrt::box_value(winrt::hstring(key)));
                row.Child(content);
                row.SizeChanged([](PGF::IInspectable const& sender, PGX::SizeChangedEventArgs const& args)
                {
                    if (const auto clip = sender.as<PGXC::Border>().Clip().try_as<PGXM::RectangleGeometry>())
                    {
                        const auto size = args.NewSize();
                        clip.Rect({ 0.0f, 0.0f, size.Width, size.Height });
                    }
                });
                if (context->appRows.IsLoaded()) row.Height(0.0);
                children.InsertAt(position, row);
                if (context->appRows.IsLoaded()) AnimateAppRow(context, row, key, false);
                ++added;
            }
            ++position;
        }
        if (added || removed || moved)
            Log(L"Mixer rows updated added={0} removed={1} moved={2}",
                { std::to_wstring(added), std::to_wstring(removed), std::to_wstring(moved) });
    }

    inline void SendSetDefault(bool render, const std::wstring& deviceId)
    {
        SendOrLog(std::string("SETDEFAULT ") + (render ? "render " : "capture ") + Narrow(deviceId) + "\n", L"SETDEFAULT");
    }

    inline bool SendClearRedirects() { return SendOrLog("CLEARREDIRECT\n", L"CLEARREDIRECT"); }


    inline PGXC::StackPanel BuildCustomPage(ContextPtr const& context);

    inline void MountFooter(ContextPtr const& context, int attempt);

    inline void Settle(ContextPtr const& context, int attempt);
    inline void CommitTakeover(ContextPtr const& context);

    inline void InstallFooterWatch(ContextPtr const& context);

    inline void RefreshFooter(ContextPtr const& context)
    {
        if (!context->active || !context->slotLeft) return;

        const bool custom = (Settings().page == 0);
        const bool recording = custom && kRecordingModeEnabled && context->recordingAvailable;
        const bool enabled = context->pendingRecording.ValueOr(context->recordingTarget.value_or(Settings().recordingMode));
        if (context->footerRow)
        {
            context->footerRow.Margin(PGX::ThicknessHelper::FromLengths(custom ? 4 : 0, 0, custom ? 4 : 0, 0));
            const std::uint32_t count = custom ? (recording ? 5 : 4) : 0;
            if (context->footerRow.ColumnDefinitions().Size() != count)
            {
                context->footerRow.ColumnDefinitions().Clear();
                for (std::uint32_t index = 0; index < count; ++index)
                {
                    PGXC::ColumnDefinition column;
                    column.Width(PGX::GridLengthHelper::FromValueAndType(1, PGX::GridUnitType::Star));
                    context->footerRow.ColumnDefinitions().Append(column);
                }
            }
            PGXC::Grid::SetColumn(context->slotLeft, 0);
            PGXC::Grid::SetColumn(context->slotEdit, custom ? 1 : 0);
            PGXC::Grid::SetColumn(context->slotMid, custom ? 2 : 0);
            PGXC::Grid::SetColumn(context->slotRecording, recording ? 3 : 0);
            PGXC::Grid::SetColumn(context->slotRight, custom ? count - 1 : 0);
        }
        const auto customVisibility = custom ? PGX::Visibility::Visible : PGX::Visibility::Collapsed;
        context->slotLeft.Visibility(customVisibility);
        context->slotEdit.Visibility(customVisibility);
        context->slotMid.Visibility(customVisibility);
        context->slotRecording.Visibility(recording ? PGX::Visibility::Visible : PGX::Visibility::Collapsed);
        const double padding = custom ? (recording ? 0 : 6) : 11;
        for (const auto& button : { context->slotLeft, context->slotEdit, context->slotMid, context->slotRecording, context->slotRight })
        {
            if (custom) button.MinWidth(0);
            else button.ClearValue(PGX::FrameworkElement::MinWidthProperty());
            button.HorizontalContentAlignment(PGX::HorizontalAlignment::Center);
            button.HorizontalAlignment(custom ? PGX::HorizontalAlignment::Stretch : PGX::HorizontalAlignment::Right);
            button.IsEnabled(!custom || !context->recordingChanging);
            button.Padding(PGX::ThicknessHelper::FromLengths(padding, 0, padding, 0));
        }
        context->slotRecording.IsEnabled(recording && !context->recordingChanging);
        static thread_local winrt::Windows::UI::ViewManagement::UISettings ui;
        const auto accent = AccentBrush();
        const auto foreground = PGXM::SolidColorBrush(ui.UIElementColor(
            winrt::Windows::UI::ViewManagement::UIElementType::HighlightText));
        for (const auto& [button, selected] : { std::pair{ context->slotRecording, enabled },
                                              std::pair{ context->slotEdit, context->editing } })
        {
            button.Background(selected ? accent : nullptr);
            if (selected) button.Foreground(foreground);
            else button.ClearValue(PGXC::Control::ForegroundProperty());
            for (auto key : { L"ButtonBackgroundPointerOver", L"ButtonBackgroundPressed" })
            {
                const auto boxed = winrt::box_value(key);
                if (selected) button.Resources().Insert(boxed, accent);
                else if (button.Resources().HasKey(boxed)) button.Resources().Remove(boxed);
            }
        }
        SetButtonLabel(context->slotEdit, context->editing ? L"page.footer.exitEditMode" : L"page.footer.editMode");
        if (context->systemItem)
        {
            context->systemItem.Visibility(custom ? PGX::Visibility::Collapsed : PGX::Visibility::Visible);
        }

        context->slotLeft.Content(winrt::box_value(winrt::hstring(text::Embedded().Resolve(L"page.footer.clearRedirect"))));
        const auto settingsGlyph = Glyph(L"\xE713", 16);
        settingsGlyph.Margin(PGX::ThicknessHelper::FromUniformLength(0));
        context->slotMid.Content(settingsGlyph);
        PGXC::ToolTipService::SetToolTip(context->slotMid, winrt::box_value(winrt::hstring(text::Embedded().Resolve(L"page.footer.settings"))));
        PGX::Automation::AutomationProperties::SetName(context->slotMid, winrt::hstring(text::Embedded().Resolve(L"page.footer.settings")));
        context->slotRight.Content(winrt::box_value(custom ? L"SystemMixer" : L"MixerExtender"));
        context->slotRight.Visibility(PGX::Visibility::Visible);
    }

    inline void ReleaseEndpointWork(ContextPtr const& context)
    {
        CloseEndpointIconPicker(context);
        context->endpointIcons.clear();
        ++context->meterGeneration;
        context->meterRendering.reset();
        context->endpointMeters.clear();
        context->endpointMonitors.clear();
    }

    inline void ReleaseRowWork(ContextPtr const& context, bool keepEndpoints = false)
    {
        for (const auto& binding : context->rows)
        {
            binding->active = false;
            binding->handle.reset();
            binding->slider = nullptr;
            binding->mute = nullptr;
        }
        context->appMeters.clear();
        if (!keepEndpoints) ReleaseEndpointWork(context);
        context->rows.clear();
    }

    inline void UpdateVolumeLayer(ContextPtr const& context, bool keepEndpoints)
    {
        if (!keepEndpoints)
        {
            context->volumeLayer.Children().SetAt(0, BuildVolumeLayer(context));
            Log(L"Device section rebuilt outputs={0} inputs={1}",
                { std::to_wstring(context->data->render.size()), std::to_wstring(context->data->capture.size()) });
            return;
        }
        context->volumeLabel.Text(winrt::hstring(text::Embedded().Resolve(
            Settings().recordingMode ? L"page.label.listenOutput" : L"page.section.volume")));
        UpdateEndpointRows(context->renderRows, context->data->render);
        UpdateEndpointRows(context->captureRows, context->data->capture);
    }

    inline void StopPageWork(ContextPtr const& context)
    {
        if (!context->active) return;
        context->active = false;
        context->abandoned = true;
        context->pendingRecording.Reset();
        context->recordingTarget.reset();
        context->pendingRenderSelection.Reset();
        context->pendingCaptureSelection.Reset();
        for (auto timer : { context->settleTimer, context->deviceTimer, context->recordingTimer,
                            context->recordingActionTimer, context->deviceSelectionTimer })
            if (timer) timer.Stop();
        context->settleTimer = nullptr;
        context->deviceTimer = nullptr;
        context->recordingTimer = nullptr;
        context->recordingActionTimer = nullptr;
        context->deviceSelectionTimer = nullptr;
        ReleaseRowWork(context);
        StopAppRowMotions(context);
        context->data->handles.clear();
        context->customRoot = nullptr;
        context->volumeLayer = nullptr;
        context->renderRows = nullptr;
        context->captureRows = nullptr;
        context->appRows = nullptr;
        context->volumeLabel = nullptr;
        context->recordingFeedback = nullptr;
        context->slotLeft = nullptr;
        context->slotEdit = nullptr;
        context->slotMid = nullptr;
        context->slotRight = nullptr;
        context->slotRecording = nullptr;
        context->footerRow = nullptr;
        context->systemItem = nullptr;
        context->savedContent = nullptr;
        context->list = nullptr;
        context->footer = nullptr;
        context->pageWindow = nullptr;
        Log(L"Sound panel unloaded; timers, audio handles and subscriptions released");
    }

    inline void MountPage(ContextPtr const& context, int page, const presentation::DeviceSnapshot* snapshot)
    {
        if (!context->active) return;
        if (context->updatingPage) { ScheduleEnvironmentRefresh(context); return; }
        const PageUpdate updating(context->updatingPage);
        Settings().page = page;
        if (!context->list) return;
        if (page != 0)
        {
            context->editing = false;
            context->pendingRecording.Reset();
            context->pendingRenderSelection.Reset();
            context->pendingCaptureSelection.Reset();
            if (context->recordingActionTimer) context->recordingActionTimer.Stop();
            if (context->deviceSelectionTimer) context->deviceSelectionTimer.Stop();
        }

        const bool keepPage = page == 0 && context->customRoot && context->list.Content() == context->customRoot;
        const bool keepEndpoints = keepPage && snapshot && context->renderedDevices &&
            presentation::CanReuseVolumeRows(*snapshot, *context->renderedDevices);
        ReleaseRowWork(context, keepEndpoints);
        if (!keepPage) StopAppRowMotions(context);

        std::wstring name = L"system";
        if (page == 0)
        {
            name = L"custom";
            LoadData(context, snapshot);
            if (keepPage)
            {
                UpdateVolumeLayer(context, keepEndpoints);
                UpdateAppRows(context);
            }
            else
            {
                auto root = BuildCustomPage(context);
                context->list.Content(root);
                context->customRoot = root;
            }
            context->renderedDevices = static_cast<const presentation::DeviceSnapshot&>(*context->data);
            StartMeterUpdates(context);
        }
        else
        {
            context->list.Content(context->savedContent);
            context->customRoot = nullptr;
            context->volumeLayer = nullptr;
            context->renderRows = nullptr;
            context->captureRows = nullptr;
            context->appRows = nullptr;
            context->volumeLabel = nullptr;
            context->renderedDevices.reset();
        }
        RefreshFooter(context);

        Log(keepEndpoints ? L"Device controls reused page={0} name={1}" : L"Page mounted page={0} name={1}", { std::to_wstring(page), name });
    }

    inline void InstallFooter(ContextPtr const& context)
    {
        context->slotLeft = FooterButton(winrt::hstring(text::Embedded().Resolve(L"page.footer.clearRedirect")));
        context->slotEdit = FooterButton(L"");
        context->slotMid = FooterButton(winrt::hstring(text::Embedded().Resolve(L"page.footer.settings")));
        context->slotRight = FooterButton(L"SystemMixer");
        context->slotRecording = FooterButton(L"");
        const auto recordingGlyph = Glyph(L"\xECCB", 16);
        recordingGlyph.Margin(PGX::ThicknessHelper::FromUniformLength(0));
        context->slotRecording.Content(recordingGlyph);
        const auto recordingName = winrt::hstring(text::Embedded().Resolve(L"page.checkbox.recordingMode"));
        PGXC::ToolTipService::SetToolTip(context->slotRecording, winrt::box_value(recordingName));
        PGXAU::AutomationProperties::SetName(context->slotRecording, recordingName);
        context->slotRecording.MinWidth(0);
        const std::weak_ptr<Context> weak = context;
        const auto editGlyph = Glyph(L"\xE70F", 16);
        editGlyph.Margin(PGX::ThicknessHelper::FromUniformLength(0));
        context->slotEdit.Content(editGlyph);
        context->slotEdit.Click([weak](auto const&, auto const&)
        {
            const auto live = weak.lock();
            if (!live || !live->active || Settings().page != 0 || !live->customRoot ||
                live->updatingPage || live->recordingChanging) return;
            live->editing = !live->editing;
            CloseEndpointIconPicker(live);
            for (const auto& icon : live->endpointIcons) SetEndpointIconEditing(icon, live->editing);
            ReleaseRowWork(live, true);
            UpdateAppRows(live);
            StartMeterUpdates(live);
            RefreshFooter(live);
        });
        context->slotRecording.Click([weak](auto const&, auto const&)
        {
            if (const auto live = weak.lock(); live && live->active && Settings().page == 0 &&
                live->recordingAvailable && !live->recordingChanging)
                QueueRecordingChange(live, !live->pendingRecording.ValueOr(Settings().recordingMode));
        });

        ContextPtr weakContext = context;
        context->slotLeft.Click([weakContext](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
        {
            if (weakContext->active && Settings().page == 0)
            {
                if (!SendClearRedirects()) return;
                ScheduleSettleSync(weakContext);
                Log(L"Clear application routes requested");
            }
        });
        context->slotMid.Click([weakContext](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
        {
            if (weakContext->active && Settings().page == 0) SendOrLog("MANAGER\n", L"MANAGER");
        });
        context->slotRight.Click([weakContext](PGF::IInspectable const&, PGX::RoutedEventArgs const&)
        {
            if (Settings().page == 0) MountPage(weakContext, 2);
            else if (Settings().page == 2) MountPage(weakContext, 0);
        });
    }

    inline void SeedRedirects(ContextPtr const& context)
    {
        PageData& data = *context->data;
        context->redirects.clear();
        if (!context->endpointPolicy) return;

        for (const auto& app : data.apps)
        {
            if (app.processId == 0 || app.appKey.empty() || app.appKey == L"#system") continue;

            std::wstring persisted;
            if (context->endpointPolicy->GetAppDefaultDevice(
                    app.processId, audio::DataFlow::Render, audio::DeviceRole::Multimedia, persisted).IsOk() &&
                !persisted.empty() && (Settings().recordingMode || persisted != data.defaultRender))
            {
                context->redirects[app.appKey] = std::move(persisted);
            }
            else
            {
                context->redirects[app.appKey] = std::wstring();
            }
        }
    }

    inline bool ReadDeviceLists(ContextPtr const& context, presentation::DeviceSnapshot& data)
    {
        Settings().showDriverName = settings::ReadBool(settings::kShowDriverName, true);
        const auto outputs = context->manager->EnumerateDevices(audio::DataFlow::Render, audio::DeviceState::Active, data.render);
        const auto routes = context->manager->EnumerateRoutingDevices(Settings().recordingMode, data.routingRender);
        const auto inputs = context->manager->EnumerateDevices(audio::DataFlow::Capture, audio::DeviceState::Active, data.capture);
        data.recordingMode = Settings().recordingMode;
        data.showDriverName = Settings().showDriverName;
        if (!outputs || !routes || !inputs) return false;
        const auto& previous = *context->data;
        presentation::SelectVisibleDefaults(data, previous, Settings().listenEndpoint);
        data.appsEndpoint = data.defaultRender;
        if (data.recordingMode)
        {
            audio::DeviceInfo virtualDevice;
            if (context->manager->FindVirtualDevice(virtualDevice)) data.appsEndpoint = std::move(virtualDevice.id);
            else data.appsEndpoint = previous.recordingMode ? previous.appsEndpoint : std::wstring{};
        }
        return true;
    }

    inline void LoadData(ContextPtr const& context, const presentation::DeviceSnapshot* snapshot)
    {
        const auto started = MeterClock::now();
        Settings().showDriverName = settings::ReadBool(settings::kShowDriverName, true);
        PageData& data = *context->data;
        if (!context->manager) return;

        if (snapshot)
        {
            static_cast<presentation::DeviceSnapshot&>(data) = *snapshot;
        }
        else
        {
            presentation::DeviceSnapshot devices;
            if (ReadDeviceLists(context, devices)) static_cast<presentation::DeviceSnapshot&>(data) = std::move(devices);
        }

        data.apps.clear();
        data.handles.clear();

        if (!Settings().recordingMode || !data.appsEndpoint.empty())
        {
            const auto scope = Settings().recordingMode ? std::wstring_view(data.appsEndpoint) : std::wstring_view();
            context->manager->EnumerateAppSessions(context->redirects, data.apps, scope);
            SeedRedirects(context);
            context->manager->EnumerateAppSessions(context->redirects, data.apps, scope, &data.handles,
                Settings().recordingMode ? audio::SessionFollowMode::Endpoint : audio::SessionFollowMode::Application);
        }

        const auto handles = std::count_if(data.handles.begin(), data.handles.end(),
            [](const auto& handle) { return static_cast<bool>(handle); });
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(MeterClock::now() - started);
        Log(L"Panel snapshot outputs={0} inputs={1} applications={2} handles={3} elapsed={4}ms",
            { std::to_wstring(data.render.size()), std::to_wstring(data.capture.size()),
              std::to_wstring(data.apps.size()), std::to_wstring(handles), std::to_wstring(elapsed.count()) });
    }

    [[nodiscard]] inline bool IsInLiveTree(PGX::DependencyObject const& node)
    {
        if (!node) return false;
        try { return PGXM::VisualTreeHelper::GetParent(node) != nullptr; } catch (...) { return false; }
    }

    inline void SyncAppTargets(ContextPtr const& context)
    {
        if (!context->active || !context->manager || !context->data) return;
        if (Settings().page != 0 || context->rows.empty()) return;
        if (!IsInLiveTree(context->list)) return;

        std::vector<audio::AppSessionInfo> live;
        if (Settings().recordingMode && context->data->appsEndpoint.empty()) return;
        if (!context->manager->EnumerateAppSessions(context->redirects, live,
            Settings().recordingMode ? std::wstring_view(context->data->appsEndpoint) : std::wstring_view()).IsOk()) return;

        for (const auto& binding : context->rows)
        {
            if (!binding || !binding->handle) continue;

            const audio::AppSessionInfo* match = nullptr;
            for (const auto& app : live)
            {
                if (app.appKey == binding->appKey)
                {
                    match = &app;
                    break;
                }
            }
            if (match == nullptr) continue;

            if (binding->handle->DeviceId() != match->deviceId || binding->handle->InstanceId() != match->instanceId)
            {
                if (binding->handle->Rebind(match->deviceId, match->instanceId).IsOk())
                {
                    Log(L"Application endpoint updated app={0} device={1}", { binding->appKey, match->deviceName });
                }
            }

            if (binding->dragging->load(std::memory_order_acquire)) continue;

            binding->synchronizing->store(true, std::memory_order_release);
            binding->slider.Value(match->volume * 100.0);
            SetMuteGlyph(binding->mute, match->muted);
            binding->synchronizing->store(false, std::memory_order_release);
        }
    }

    inline void SyncSettle(ContextPtr const& context)
    {
        if (context->settleTimer) context->settleTimer.Stop();
        if (Settings().page != 0 || !IsInLiveTree(context->list)) return;
        if (context->recordingChanging || context->pendingRecording || context->pendingRenderSelection ||
            context->pendingCaptureSelection || context->environmentRefreshRequired)
        {
            ScheduleSettleSync(context);
            return;
        }
        const auto previous = context->redirects;
        SeedRedirects(context);
        if (previous != context->redirects)
        {
            context->environmentRefreshRequired = true;
            ScheduleEnvironmentRefresh(context);
            return;
        }
        SyncAppTargets(context);
    }

    inline void ScheduleSettleSync(ContextPtr const& context)
    {
        if (!context->list) return;

        if (!context->settleTimer)
        {
            context->settleTimer = PGX::DispatcherTimer();
            context->settleTimer.Interval(std::chrono::milliseconds(700));

            ContextPtr weakContext = context;
            context->settleTimer.Tick([weakContext](PGF::IInspectable const&, PGF::IInspectable const&)
            {
                SyncSettle(weakContext);
            });
        }

        context->settleTimer.Stop();
        context->settleTimer.Start();
    }

    inline void RefreshEnvironmentNow(ContextPtr const& context)
    {
        if (!context->active || !context->list) return;
        if (context->deviceTimer) context->deviceTimer.Stop();

        if (!IsInLiveTree(context->list)) return;
        if (context->updatingPage)
        {
            ScheduleEnvironmentRefresh(context);
            return;
        }
        if (context->pendingRenderSelection || context->pendingCaptureSelection) return;

        if (Settings().page == 0)
        {
            presentation::DeviceSnapshot candidate;
            if (!ReadDeviceLists(context, candidate))
            {
                ScheduleEnvironmentRefresh(context, std::chrono::milliseconds(150));
                return;
            }
            if (context->renderedDevices && context->volumeLayer &&
                !presentation::SameVolumeRows(candidate, *context->renderedDevices))
            {
                const PageUpdate updating(context->updatingPage);
                auto& rendered = *context->renderedDevices;
                const bool keepEndpoints = presentation::CanReuseVolumeRows(candidate, rendered);
                if (!keepEndpoints) ReleaseEndpointWork(context);
                context->data->render = candidate.render;
                context->data->capture = candidate.capture;
                context->data->defaultRender = candidate.defaultRender;
                context->data->defaultCapture = candidate.defaultCapture;
                UpdateVolumeLayer(context, keepEndpoints);
                StartMeterUpdates(context);
                rendered.render = std::move(candidate.render);
                rendered.capture = std::move(candidate.capture);
                rendered.showDriverName = candidate.showDriverName;
                context->environmentRefreshRequired = true;
                if (!context->recordingChanging) ScheduleEnvironmentRefresh(context);
                return;
            }
            if (context->recordingChanging) return;
            if (std::any_of(context->rows.begin(), context->rows.end(), [](const auto& row)
                { return row->dragging->load(std::memory_order_acquire); }))
            {
                ScheduleEnvironmentRefresh(context, std::chrono::milliseconds(150));
                return;
            }
            if (context->environmentRefreshRequired || !context->renderedDevices ||
                !presentation::SameDeviceSnapshot(candidate, *context->renderedDevices))
            {
                context->environmentRefreshRequired = false;
                MountPage(context, 0, &candidate);
            }
            return;
        }

        LoadData(context);
    }

    inline void ScheduleEnvironmentRefresh(ContextPtr const& context, std::chrono::milliseconds delay)
    {
        if (!context->active || !context->list) return;

        if (!context->deviceTimer)
        {
            context->deviceTimer = PGX::DispatcherTimer();
            const std::weak_ptr<Context> weak = context;
            context->deviceTimer.Tick([weak](PGF::IInspectable const&, PGF::IInspectable const&)
            {
                if (const auto live = weak.lock()) RefreshEnvironmentNow(live);
            });
        }
        if (context->deviceTimer.IsEnabled())
        {
            if (context->deviceTimer.Interval() <= delay) return;
            context->deviceTimer.Stop();
        }
        context->deviceTimer.Interval(delay);
        context->deviceTimer.Start();
    }

    inline void RefreshEnvironment(ContextPtr const& context, const audio::DeviceEvent& event)
    {
        if (event.kind == audio::DeviceEventKind::PropertyChanged) return;
        if (!IsInLiveTree(context->list)) return;

        if (event.kind == audio::DeviceEventKind::DefaultChanged)
        {
            Log(L"Default endpoint changed device={0}", { event.deviceId });
        }
        else
        {
            Log(L"Endpoint list changed device={0}", { event.deviceId });
        }

        ScheduleEnvironmentRefresh(context);
    }

    inline void EnsureDeviceWatcher(ContextPtr const& context)
    {
        Settings().live = context;
        if (Settings().watcher || !context->list) return;

        auto dispatcher = context->list.Dispatcher();
        if (!dispatcher) return;

        auto watcher = audio::CreateAudioDeviceWatcher();
        if (!watcher) return;

        const Status started = watcher->Start([dispatcher](const audio::DeviceEvent& event)
        {
            try
            {
                dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Low, [event]()
                {
                    ContextPtr context = Settings().live.lock();
                    if (context) RefreshEnvironment(context, event);
                });
            }
            catch (...) {}
        });

        if (!started.IsOk()) return;

        Settings().watcher = watcher.release();
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
        Log(L"Sound page entry activated; awaiting content");
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
            Log(L"PageWindow missing; takeover deferred");
            return false;
        }

        PGX::FrameworkElement listElement = FindByNameDeep(pageWindow, L"ListContent", 0);
        PGXC::ScrollViewer scroller = listElement ? listElement.try_as<PGXC::ScrollViewer>() : nullptr;
        if (!scroller)
        {
            Log(L"List content missing; takeover deferred");
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
        const std::weak_ptr<Context> weak = context;
        context->pageWindow.Unloaded([weak](auto const&, auto const&)
        {
            if (const auto live = weak.lock()) StopPageWork(live);
        });

        InstallFooterWatch(context);

        if (ConsumeSoundPageExpectation())
        {
            CommitTakeover(context);
            return true;
        }

        Log(L"Waiting for sound page content");
        Settle(context, 0);
        return true;
    }

    inline void CommitTakeover(ContextPtr const& context)
    {
        if (!context->active || context->committed) return;
        context->committed = true;

        Settings().showDriverName = settings::ReadBool(settings::kShowDriverName, true);

        try { context->savedContent = context->list.Content(); } catch (...) {}

        context->recordingAvailable = Settings().recordingAvailable;
        recording::PanelState panel;
        if (recording::PanelStateCache::Read(panel))
        {
            context->recordingAvailable = Settings().recordingAvailable = panel.available;
            Settings().recordingMode = panel.recording.enabled || panel.recording.cleanupPending;
            Settings().listenEndpoint = panel.recording.monitorDevice;
        }

        InstallFooter(context);
        MountPage(context, 0);
        EnsureDeviceWatcher(context);
        if (kRecordingModeEnabled) RefreshRecording(context, L"status");

        MountFooter(context, 0);
        Log(L"Sound page takeover originalContent={0}", { context->savedContent ? L"saved" : L"empty" });
    }

    inline void Settle(ContextPtr const& context, int attempt)
    {
        if (!context->active || context->abandoned) return;

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
                Log(L"Sound page content detected");
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
            Log(L"Unrelated page skipped title={0} elapsed={1}ms", { title, std::to_wstring(elapsed) });
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
        if (!context->active) return false;
        if (context->mountDone) return true;
        if (context->mounting) return false;

        context->mounting = true;
        const FooterMount mount = TapPageMountFooterRow(
            context->footer, { context->slotLeft, context->slotEdit, context->slotMid, context->slotRecording, context->slotRight });
        context->mounting = false;

        if (mount.ok)
        {
            context->systemItem = mount.systemItem;
            context->footerRow = mount.row;
            context->mountDone = true;
            RefreshFooter(context);
            Log(L"Footer mounted elapsed={0}ms", { std::to_wstring(::GetTickCount64() - context->startedTick) });
            return true;
        }

        const std::wstring state = TapPageDescribeNode(context->footer);
        if (state != context->lastFooterState)
        {
            Log(L"Footer mount failed attempt={0} elapsed={1}ms detail={2} node={3}", { std::to_wstring(context->mountAttempts),
                                                    std::to_wstring(::GetTickCount64() - context->startedTick),
                                                    strings::ToWide(mount.reason), state });
            context->lastFooterState = state;
        }
        context->mountAttempts += 1;
        return false;
    }

    inline void MountFooter(ContextPtr const& context, int attempt)
    {
        if (!context->active || !context->footer) return;
        if (TryMountNow(context)) return;

        constexpr unsigned long long kBudgetMs = 6000;
        const unsigned long long elapsed = ::GetTickCount64() - context->startedTick;
        if (elapsed > kBudgetMs)
        {
            if (!context->mountGaveUp)
            {
                context->mountGaveUp = true;
                Log(L"Footer mounting deferred to event hook elapsed={0}ms attempts={1}",
                       { std::to_wstring(elapsed), std::to_wstring(context->mountAttempts) });
                TapPageDumpTree(context->footer, 14);
            }
            return;
        }

        auto dispatcher = context->footer.Dispatcher();
        if (!dispatcher)
        {
            Log(L"Footer dispatcher missing; mounting skipped");
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
            Log(L"Footer retry dispatch failed");
        }
    }

    inline void InstallFooterWatch(ContextPtr const& context)
    {
        std::weak_ptr<Context> weak = context;

        context->footer.SizeChanged([weak](PGF::IInspectable const&, PGX::SizeChangedEventArgs const& args)
        {
            if (auto live = weak.lock())
            {
                Log(L"Footer size changed width={0} height={1}", { strings::FromDouble(args.NewSize().Width, 0),
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
                    Log(L"Footer first layout completed");
                }
                Settle(live, 0);
            }
        });
    }

    inline winrt::fire_and_forget RefreshRecording(ContextPtr context, std::wstring action, std::wstring device)
    {
        winrt::Windows::UI::Core::CoreDispatcher dispatcher{nullptr};
        try
        {
            if (!context->active || context->recordingBusy || !context->list || Settings().page != 0) co_return;
            context->recordingBusy = true;
            const bool changing = action != L"status";
            context->recordingChanging = changing;
            if (action == L"on" || action == L"off") context->recordingTarget = action == L"on";
            dispatcher = context->list.Dispatcher();
            if (changing) context->list.IsHitTestVisible(false);
            RefreshFooter(context);
            recording::Snapshot state;
            Status result;
            bool available = context->recordingAvailable;
            try
            {
                co_await winrt::resume_background();
                recording::PanelState panel;
                if (!changing && recording::PanelStateCache::Read(panel))
                {
                    state = panel.recording;
                    available = panel.available;
                }
                else
                {
                    result = recording::Request(action, device, state);
                    if (recording::PanelStateCache::Read(panel)) available = panel.available;
                    else if (state.known)
                        available = recording::QueryRecordingReadiness(state.enabled || state.cleanupPending ?
                            std::wstring_view(state.monitorDevice) : std::wstring_view()).IsOk();
                }
            }
            catch (...) { result = Status::Failed(L"recording-request-exception"); }
            co_await winrt::resume_foreground(dispatcher);
            context->recordingBusy = false;
            context->recordingChanging = false;
            context->recordingTarget.reset();
            if (!context->active || !context->list || Settings().page != 0) co_return;
            if (changing) context->list.IsHitTestVisible(true);
            if (!IsInLiveTree(context->list)) co_return;
            bool changed = action == L"monitor" || (state.known &&
                (Settings().recordingMode != (state.enabled || state.cleanupPending) || Settings().listenEndpoint != state.monitorDevice));
            if (context->recordingAvailable != available)
                Log(L"Recording availability changed available={0}", { available ? L"1" : L"0" });
            context->recordingAvailable = Settings().recordingAvailable = available;
            if (state.known)
            {
                Settings().recordingMode = state.enabled || state.cleanupPending;
                Settings().listenEndpoint = state.monitorDevice;
            }
            RefreshFooter(context);
            if (!changed && state.known && state.enabled && context->manager && context->data &&
                !context->data->appsEndpoint.empty())
            {
                std::vector<audio::AppSessionInfo> live;
                if (context->manager->EnumerateAppSessions(context->redirects, live, context->data->appsEndpoint).IsOk())
                {
                    changed = live.size() != context->data->apps.size();
                    for (const auto& session : live)
                    {
                        if (std::none_of(context->data->apps.begin(), context->data->apps.end(), [&](const auto& old)
                            { return old.instanceId == session.instanceId && old.deviceId == session.deviceId; }))
                        {
                            changed = true;
                            break;
                        }
                    }
                }
                if (changed && std::any_of(context->rows.begin(), context->rows.end(), [](const auto& row)
                    { return row->dragging->load(std::memory_order_acquire); })) changed = false;
            }
            if (changed) context->environmentRefreshRequired = true;
            if (changed || changing) ScheduleEnvironmentRefresh(context);
            if (!result) Log(L"Recording state query failed detail={0}", { result.detail });
            if (context->recordingFeedback)
            {
                context->recordingFeedback.Text(result ? L"" : winrt::hstring(text::Embedded().ResolveFormat(L"recording.failed", { result.detail })));
                context->recordingFeedback.Visibility(result || !available ? PGX::Visibility::Collapsed : PGX::Visibility::Visible);
            }
        }
        catch (...)
        {
            Log(L"Recording UI refresh failed; exception contained");
            try
            {
                if (dispatcher && dispatcher.HasThreadAccess())
                {
                    context->recordingBusy = false;
                    context->recordingChanging = false;
                    context->recordingTarget.reset();
                    if (context->active && context->list)
                    {
                        context->list.IsHitTestVisible(true);
                        RefreshFooter(context);
                    }
                }
            }
            catch (...) {}
        }
    }

    inline PGXC::StackPanel BuildCustomPage(ContextPtr const& context)
    {
        PGXC::StackPanel root;
        root.Padding(PGX::ThicknessHelper::FromLengths(kBodyInset, 0, kBodyInset, 0));
        if (kRecordingModeEnabled)
        {
            const std::weak_ptr<Context> weak = context;
            PGXC::TextBlock feedback;
            feedback.TextWrapping(PGX::TextWrapping::Wrap);
            feedback.FontSize(12);
            feedback.Visibility(PGX::Visibility::Collapsed);
            context->recordingFeedback = feedback;
            root.Children().Append(feedback);
            if (!context->recordingTimer)
            {
                context->recordingTimer = PGX::DispatcherTimer();
                context->recordingTimer.Interval(std::chrono::seconds(2));
                context->recordingTimer.Tick([weak](auto const&, auto const&)
                {
                    if (const auto live = weak.lock(); live && Settings().page == 0 && IsInLiveTree(live->list))
                        RefreshRecording(live, L"status");
                });
                context->recordingTimer.Start();
            }
        }
        context->volumeLayer = PGXC::Grid();
        context->volumeLayer.Children().Append(BuildVolumeLayer(context));
        root.Children().Append(context->volumeLayer);

        PGXC::StackPanel mixer;
        mixer.Children().Append(SectionLabel(winrt::hstring(text::Embedded().Resolve(L"page.section.mixer"))));
        context->appRows = PGXC::StackPanel();
        context->appRows.Margin(PGX::ThicknessHelper::FromLengths(kAudioRowLeadingInset, 0, 0, 0));
        UpdateAppRows(context);
        mixer.Children().Append(context->appRows);
        root.Children().Append(mixer);
        return root;
    }

}
