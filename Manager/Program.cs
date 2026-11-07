using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using System.Windows.Shell;
using System.Windows.Documents;

namespace Vmex.Manager;

internal static class Core
{
    private const string Dll = "vmex_core.dll";
    [DllImport(Dll, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)] internal static extern int VmexManagerInitialize(StringBuilder error, int capacity);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] internal static extern void VmexManagerShutdown();
    [DllImport(Dll, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)] private static extern void VmexManagerText(string key, StringBuilder text, int capacity);
    [DllImport(Dll, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)] private static extern void VmexManagerAbout(StringBuilder text, int capacity);
    [DllImport(Dll, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)] private static extern void VmexManagerVersion(StringBuilder text, int capacity);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] internal static extern int VmexManagerQuery([Out] int[] values, int count);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] internal static extern int VmexRecordingDriverQuery([Out] int[] values, int count);
    [DllImport(Dll, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)] internal static extern int VmexManagerExecute(int action, int value, StringBuilder error, int capacity);
    [DllImport(Dll, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)] internal static extern int VmexManagerUpdate(int action, out int available, StringBuilder error, int capacity);
    internal static string Text(string key) { var text = new StringBuilder(4096); VmexManagerText(key, text, text.Capacity); return text.ToString(); }
    internal static string About() { var text = new StringBuilder(16384); VmexManagerAbout(text, text.Capacity); return text.ToString(); }
    internal static string Version() { var text = new StringBuilder(128); VmexManagerVersion(text, text.Capacity); return text.ToString(); }
}

internal static class Program
{
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(nint hwnd);
    [DllImport("user32.dll")] private static extern bool ShowWindow(nint hwnd, int command);
    internal static void BringToForeground(nint hwnd) { ShowWindow(hwnd, 9); SetForegroundWindow(hwnd); }
    [STAThread]
    private static int Main(string[] args)
    {
        bool test = args.Contains("--self-test");
        using var mutex = new Mutex(true, $"Local\\VmExt.Manager.S{Process.GetCurrentProcess().SessionId}{(test ? ".SelfTest" : "")}", out bool first);
        if (!first)
        {
            foreach (var process in Process.GetProcessesByName("vmex_manager"))
                if (process.Id != Process.GetCurrentProcess().Id && process.SessionId == Process.GetCurrentProcess().SessionId && process.MainWindowHandle != IntPtr.Zero)
                { BringToForeground(process.MainWindowHandle); break; }
            return 0;
        }
        bool initialized = false;
        try
        {
            if (!test)
            {
                var error = new StringBuilder(4096);
                if (Core.VmexManagerInitialize(error, error.Capacity) != 0) throw new InvalidOperationException(error.ToString());
                initialized = true;
            }
            var app = new Application { ShutdownMode = ShutdownMode.OnMainWindowClose };
            app.Resources.MergedDictionaries.Add(new ResourceDictionary { Source = new Uri("/vmex_manager;component/Theme.xaml", UriKind.Relative) });
            Theme.Apply();
            var window = new ManagerWindow(test, args.Contains("--update-failed"));
            return app.Run(window);
        }
        catch (Exception error)
        {
            if (test) Console.Error.WriteLine(error);
            else MessageBox.Show(Core.Text("manager.failed").Replace("{0}", error.Message), Core.Text("manager.title"), MessageBoxButton.OK, MessageBoxImage.Error);
            return 3;
        }
        finally { if (initialized) Core.VmexManagerShutdown(); }
    }
}

internal sealed class ManagerWindow : Window
{
    private readonly TextBlock status = new(), feedback = new();
    private readonly Button host = new(), panel = new();
    private readonly Button softwareUpdate = new();
    private readonly TextBlock driverStatus = new(), driverRestart, driverHint;
    private readonly Button installDriver = new(), uninstallDriver = new();
    private readonly CheckBox autostart = new(), driver = new();
    private readonly StackPanel body = new();
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(2) };
    private readonly DispatcherTimer releaseFocusTimer = new() { Interval = TimeSpan.FromSeconds(10) };
    private Task? stateRefresh;
    private bool busy, updating, running, modal, retryInjection;
    private bool updateAvailable, updateLaunched, releaseFocusPending;
    private readonly bool test;

    internal ManagerWindow(bool selfTest, bool updateFailed = false)
    {
        test = selfTest;
        Title = Core.Text("manager.title"); Width = 600; SizeToContent = SizeToContent.Height;
        WindowStyle = WindowStyle.SingleBorderWindow;
        ResizeMode = ResizeMode.NoResize;
        WindowChrome.SetWindowChrome(this, new WindowChrome { CaptionHeight = 64, ResizeBorderThickness = SystemParameters.WindowResizeBorderThickness, GlassFrameThickness = new Thickness(0, 64, 0, 0), UseAeroCaptionButtons = true });
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        Background = Brushes.Transparent;
        SetResourceReference(ForegroundProperty, "TextBrush");
        SourceInitialized += (_, _) => Theme.Apply(this);
        SetResourceReference(FontFamilyProperty, SystemFonts.MessageFontFamilyKey);
        SetResourceReference(FontSizeProperty, SystemFonts.MessageFontSizeKey);
        FontWeight = FontWeights.Normal;
        UseLayoutRounding = true; SnapsToDevicePixels = true;
        TextOptions.SetTextFormattingMode(this, TextFormattingMode.Display);
        TextOptions.SetTextRenderingMode(this, TextRenderingMode.Auto);
        var layout = new Grid(); layout.RowDefinitions.Add(new() { Height = new GridLength(64) }); layout.RowDefinitions.Add(new() { Height = GridLength.Auto });
        var surface = new Border { IsHitTestVisible = false };
        surface.SetResourceReference(Border.BackgroundProperty, "WindowBrush");
        Grid.SetRowSpan(surface, 2); layout.Children.Add(surface);
        var frame = new NativeFrame(this, surface);
        var about = new Button
        {
            Content = new TextBlock { Text = "\uE946", FontFamily = new FontFamily("Segoe Fluent Icons"), FontSize = 16 },
            Width = 40, Height = 32, Padding = new Thickness(0), BorderThickness = new Thickness(0),
            Background = Brushes.Transparent, HorizontalAlignment = HorizontalAlignment.Right,
            VerticalAlignment = VerticalAlignment.Top, ToolTip = Core.Text("about.title")
        };
        System.Windows.Automation.AutomationProperties.SetName(about, Core.Text("about.title"));
        WindowChrome.SetIsHitTestVisibleInChrome(about, true);
        var caption = new StackPanel
        {
            Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right,
            VerticalAlignment = VerticalAlignment.Top, Height = 32
        };
        var version = Label("manager.version", 12, FontWeights.Normal, true);
        version.Text = version.Text.Replace("{0}", Core.Version());
        version.FontStyle = FontStyles.Normal;
        version.VerticalAlignment = VerticalAlignment.Center;
        version.Margin = new Thickness(0, 0, 8, 0);
        caption.Children.Add(version);
        caption.Children.Add(about);
        frame.PlaceBeforeCaption(caption);
        about.Click += (_, _) => ShowAbout();
        var header = new Grid { Margin = new Thickness(24, 0, 160, 0) };
        var heading = Label("manager.title", 22, FontWeights.Normal); heading.Margin = new Thickness(0); heading.VerticalAlignment = VerticalAlignment.Center; header.Children.Add(heading);
        layout.Children.Add(header);
        var ready = Label("manager.ready", 12, FontWeights.Normal, true); ready.HorizontalAlignment = HorizontalAlignment.Right; ready.VerticalAlignment = VerticalAlignment.Bottom; ready.Margin = new Thickness(0, 0, 12, 6); ready.TextWrapping = TextWrapping.NoWrap;
        Grid.SetRow(ready, 0); layout.Children.Add(ready);
        layout.Children.Add(caption);
        body.Margin = new Thickness(24, 4, 24, 12);
        Grid.SetRow(body, 1); layout.Children.Add(body); Content = layout;
        var runtime = new Grid(); runtime.ColumnDefinitions.Add(new()); runtime.ColumnDefinitions.Add(new() { Width = GridLength.Auto });
        var texts = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        status.FontSize = 18; status.FontWeight = FontWeights.Normal; status.Margin = new Thickness(0, 0, 0, 4);
        status.Text = Core.Text("manager.loading"); texts.Children.Add(status);
        texts.Children.Add(PanelHint());
        runtime.Children.Add(texts);
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        panel.Content = Core.Text("manager.panel"); panel.Padding = new Thickness(14, 7, 14, 7); panel.Margin = new Thickness(0, 0, 8, 0);
        panel.SetResourceReference(StyleProperty, "AccentButtonStyle");
        panel.Click += async (_, _) => await Execute(retryInjection ? 13 : 8); buttons.Children.Add(panel);
        host.Content = Core.Text("manager.start"); host.Padding = new Thickness(14, 7, 14, 7);
        host.Click += async (_, _) => { if (running && !Confirm("manager.stop", "manager.stop.confirm")) return; await Execute(running ? 2 : 1); };
        buttons.Children.Add(host);
        Grid.SetColumn(buttons, 1); runtime.Children.Add(buttons);
        body.Children.Add(Card(runtime, 14, 8));
        body.Children.Add(Label("manager.settings", 14, FontWeights.SemiBold));
        body.Children.Add(Setting("\uE7E8", "page.toggle.autostart", "manager.autostart.hint", autostart));
        body.Children.Add(Setting("\uE7F4", "page.toggle.showDriverName", "manager.driver.hint", driver));
        autostart.Click += async (_, _) => { if (!updating) await Execute(3, autostart.IsChecked == true ? 1 : 0); };
        driver.Click += async (_, _) => { if (!updating) await Execute(4, driver.IsChecked == true ? 1 : 0); };
        body.Children.Add(Label("manager.maintenance", 14, FontWeights.SemiBold));
        var driverCard = new Grid();
        driverCard.ColumnDefinitions.Add(new());
        driverCard.ColumnDefinitions.Add(new() { Width = GridLength.Auto });
        var driverDetails = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 12, 0) };
        driverStatus.Text = Core.Text("manager.loading");
        driverStatus.TextWrapping = TextWrapping.Wrap;
        driverDetails.Children.Add(driverStatus);
        var driverActions = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        var recordingInfo = new Button
        {
            Content = new TextBlock { Text = "\uE946", FontFamily = new FontFamily("Segoe Fluent Icons"), FontSize = 16 },
            Width = 30, Height = 30, Padding = new Thickness(0), Margin = new Thickness(0, 0, 8, 0),
            Background = Brushes.Transparent, BorderThickness = new Thickness(0)
        };
        System.Windows.Automation.AutomationProperties.SetName(recordingInfo, Core.Text("recording.info.title"));
        var recordingTip = new ToolTip
        {
            Content = new TextBlock { Text = Core.Text("recording.info"), MaxWidth = 360, TextWrapping = TextWrapping.Wrap },
            PlacementTarget = recordingInfo, Placement = PlacementMode.Bottom
        };
        recordingInfo.ToolTip = recordingTip;
        ToolTipService.SetInitialShowDelay(recordingInfo, 300);
        ToolTipService.SetShowDuration(recordingInfo, 60000);
        recordingInfo.Click += (_, _) => recordingTip.IsOpen = true;
        recordingInfo.LostKeyboardFocus += (_, _) => recordingTip.IsOpen = false;
        driverActions.Children.Add(recordingInfo);
        installDriver.Content = Core.Text("driver.install"); installDriver.Padding = new Thickness(12, 6, 12, 6);
        uninstallDriver.Content = Core.Text("driver.uninstall"); uninstallDriver.Padding = new Thickness(12, 6, 12, 6);
        installDriver.IsEnabled = false; uninstallDriver.IsEnabled = false;
        installDriver.Visibility = Visibility.Collapsed; uninstallDriver.Visibility = Visibility.Collapsed;
        installDriver.Click += async (_, _) => await Execute(10);
        uninstallDriver.Click += async (_, _) => { if (Confirm("driver.uninstall", "driver.uninstall.confirm")) await Execute(11); };
        driverActions.Children.Add(installDriver); driverActions.Children.Add(uninstallDriver);
        Grid.SetColumn(driverActions, 1); driverCard.Children.Add(driverActions);
        driverRestart = Label("driver.restart.required", 13, FontWeights.SemiBold);
        driverRestart.SetResourceReference(TextBlock.ForegroundProperty, "AccentBrush");
        driverRestart.Margin = new Thickness(0, 4, 0, 0);
        driverRestart.Visibility = Visibility.Collapsed;
        driverDetails.Children.Add(driverRestart);
        driverHint = Label("driver.hint", 12, FontWeights.Normal, true);
        driverHint.Margin = new Thickness(0, 4, 0, 0);
        driverDetails.Children.Add(driverHint);
        driverCard.Children.Add(driverDetails);
        body.Children.Add(Card(driverCard, 14, 8));
        var links = new Grid();
        void AddAction(Button button)
        {
            if (links.Children.Count > 0)
                links.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(8) });
            Grid.SetColumn(button, links.ColumnDefinitions.Count);
            links.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            button.Padding = new Thickness(6, 8, 6, 8);
            links.Children.Add(button);
        }
        Button AddLink(string key, int action)
        {
            var button = new Button { Content = Core.Text(key) };
            button.Click += async (_, _) => await Execute(action);
            AddAction(button);
            return button;
        }
        AddLink("manager.startmenu", 9);
        var uninstall = new Button { Content = Core.Text("manager.uninstall") };
        uninstall.Click += async (_, _) => { if (Confirm("manager.uninstall", "manager.uninstall.confirm")) await Execute(7); };
        AddAction(uninstall);
        AddLink("manager.logs", 5);
        var project = AddLink("manager.project", 6);
        softwareUpdate.Content = Core.Text("manager.update.check");
        softwareUpdate.Click += async (_, _) => await UpdateSoftware();
        AddAction(softwareUpdate);
        body.Children.Add(Card(links, 14));
        feedback.TextWrapping = TextWrapping.Wrap;
        feedback.MinHeight = 20;
        feedback.Margin = new Thickness(0, 4, 0, 0);
        body.Children.Add(feedback);
        timer.Tick += async (_, _) =>
        {
            Theme.Apply(this);
            if (updateLaunched) await PollUpdate();
            else await Update();
        };
        Closing += (_, args) => { if (busy || modal || stateRefresh is { IsCompleted: false }) args.Cancel = true; };
        Deactivated += (_, _) => ReturnFromRelease();
        releaseFocusTimer.Tick += (_, _) => ClearReleaseFocus();
        Closed += (_, _) => { timer.Stop(); ClearReleaseFocus(); };
        if (updateFailed) feedback.Text = Core.Text("manager.update.installFailed");
        Loaded += async (_, _) =>
        {
            if (test)
            {
                try
                {
                    var invalid = new StringBuilder(32);
                    if (await Task.Run(() => Core.VmexManagerExecute(99, 0, invalid, invalid.Capacity)) != 1 || invalid.ToString() != "management-action") throw new InvalidOperationException("Core management ABI mismatch");
                    if (await Task.Run(() => Core.VmexManagerExecute(13, 0, invalid, invalid.Capacity)) != 3 || invalid.ToString() != "not-ready")
                        throw new InvalidOperationException("Reinjection without an initialized Core must be rejected before changing the host");
                    if (TryFindResource("ToggleSwitchCheckBoxStyle") is not System.Windows.Style || TryFindResource("AccentButtonStyle") is not System.Windows.Style) throw new InvalidOperationException("Control styles unavailable");
                    Present(new[] { 1, 0, 1, 0, 1, 0 });
                    if (status.Text != Core.Text("manager.panel.ready")) throw new InvalidOperationException("A transient pipe connection failure must not mark a running host as stopped");
                    Present(new[] { 0, 0, 0, 0, 1, 0 });
                    if (status.Text != Core.Text("manager.state.stopped") || panel.Content.ToString() != Core.Text("manager.panel")) throw new InvalidOperationException("Stopped host status mismatch");
                    Present(new[] { 1, 1, 0, 0, 1, 0 });
                    if (status.Text != Core.Text("manager.panel.preparing") || panel.Content.ToString() != Core.Text("manager.reinject")) throw new InvalidOperationException("Panel readiness status mismatch");
                    PresentDriver(new[] { 0, 0, 1, 0, 0 }); UpdateLayout();
                    await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    Preview("not-ready");
                    Present(new[] { 1, 1, 1, 0, 1, 0 });
                    if (panel.Content.ToString() != Core.Text("manager.panel")) throw new InvalidOperationException("A ready panel must offer the open action");
                    PresentDriver(new[] { 0, 0, 0, 0, 0 });
                    if (installDriver.IsEnabled || uninstallDriver.IsEnabled) throw new InvalidOperationException("Missing driver package must not offer installation");
                    PresentDriver(new[] { 1, 1, 0, 0, 0 });
                    if (installDriver.Visibility != Visibility.Collapsed || !uninstallDriver.IsEnabled || uninstallDriver.Visibility != Visibility.Visible) throw new InvalidOperationException("Installed driver must only offer removal");
                    PresentDriver(new[] { 0, 0, 1, 0, 1 });
                    if (driverRestart.Visibility != Visibility.Visible || driverHint.Visibility != Visibility.Collapsed || installDriver.IsEnabled || uninstallDriver.IsEnabled)
                        throw new InvalidOperationException("A pending restart must be visible and block reinstallation");
                    UpdateLayout();
                    await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    Preview("driver-restart");
                    PresentDriver(new[] { 1, 1, 1, 0, 1 });
                    if (driverRestart.Visibility != Visibility.Visible || installDriver.IsEnabled || uninstallDriver.IsEnabled)
                        throw new InvalidOperationException("A pending restart must also block removal");
                    PresentDriver(new[] { 0, 0, 1, 0, 0 });
                    if (!installDriver.IsEnabled || installDriver.Visibility != Visibility.Visible || uninstallDriver.Visibility != Visibility.Collapsed ||
                        driverRestart.Visibility != Visibility.Collapsed || driverHint.Visibility != Visibility.Visible)
                        throw new InvalidOperationException("Clearing the pending restart must restore installation");
                    if (WindowStyle != WindowStyle.SingleBorderWindow || !WindowChrome.GetWindowChrome(this).UseAeroCaptionButtons || Title != Core.Text("manager.title")) throw new InvalidOperationException("System window chrome unavailable");
                    if (!WindowChrome.GetIsHitTestVisibleInChrome(about) || Core.About().Contains("VMEX_VERSION_STRING") || !Core.About().Contains("VB-CABLE")) throw new InvalidOperationException("About information or caption hit testing unavailable");
                    updating = true; driver.IsChecked = true; updating = false;
                    Theme.Apply(this, false); UpdateLayout();
                    await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    Preview("light");
                    Theme.Apply(this, true); UpdateLayout(); Preview("dark"); Theme.Apply(this);
                    recordingInfo.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                    await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    if (!recordingTip.IsOpen || recordingTip.ActualHeight <= 0 || recordingInfo.ToolTip != recordingTip)
                        throw new InvalidOperationException("Recording information must use the same tooltip for hover and click");
                    Preview(recordingTip, "recording-info");
                    recordingTip.IsOpen = false;
                    if (softwareUpdate.TranslatePoint(new Point(), links).Y > 1 ||
                        softwareUpdate.TranslatePoint(new Point(), links).X <= project.TranslatePoint(new Point(), links).X ||
                        Math.Abs(softwareUpdate.TranslatePoint(new Point(softwareUpdate.ActualWidth, 0), links).X - links.ActualWidth) > 1 ||
                        links.Children.Cast<Button>().Any(button => Math.Abs(button.ActualWidth - softwareUpdate.ActualWidth) > 1))
                        throw new InvalidOperationException("Maintenance buttons must share the available row width equally");
                    var updateError = new StringBuilder(4096);
                    if (await Task.Run(() => Core.VmexManagerUpdate(2, out _, updateError, updateError.Capacity)) == 0 ||
                        updateError.ToString() != "update-not-checked")
                        throw new InvalidOperationException("Installing without a checked release must be rejected");
                    PresentUpdate(true); UpdateLayout(); Preview("update-available");
                    if (softwareUpdate.Content.ToString() != Core.Text("manager.update.download") ||
                        feedback.Text != Core.Text("manager.update.available"))
                        throw new InvalidOperationException("An available update must offer download and retain feedback");
                    PresentUpdate(false); feedback.Text = string.Empty;
                    if (ActualWidth < 580 || status.Text.Length == 0 || host.Template is null || autostart.Template is null) throw new InvalidOperationException("WPF layout or resources unavailable");
                    feedback.Text = Core.Text("manager.done"); UpdateLayout();
                    await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    double compactHeight = ActualHeight;
                    feedback.Text = string.Join("\n", Enumerable.Repeat(Core.Text("driver.restart.required"), 6));
                    UpdateLayout();
                    await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    if (SizeToContent != SizeToContent.Height || ActualHeight < compactHeight + 60 ||
                        feedback.TranslatePoint(new Point(0, feedback.ActualHeight), layout).Y > layout.ActualHeight)
                        throw new InvalidOperationException("Long feedback must expand the window to fit its content");
                    Preview("long-message");
                    feedback.Text = string.Empty; UpdateLayout();
                    await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    if (Math.Abs(ActualHeight - compactHeight) > 1)
                        throw new InvalidOperationException("Clearing feedback must restore the compact window height");
                    if (!frame.HasTransparentCaptionArea) throw new InvalidOperationException("Native caption buttons are covered by the client background");
                    var aboutWindow = CreateAbout();
                    aboutWindow.Show(); await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    if (aboutWindow.Content is not StackPanel aboutContent || aboutContent.Children.Count != 2 ||
                        aboutContent.Children[0] is not Border projectCard || aboutContent.Children[1] is not Border { Child: StackPanel thirdParty } ||
                        thirdParty.Children.Count != 2 || thirdParty.Children[0] is not TextBlock thirdPartyTitle ||
                        thirdPartyTitle.Text != Core.Text("about.thirdParty") || thirdParty.Children[1] is not Grid components || components.Children.Count != 2 ||
                        components.Children[0] is not Border microsoftCard || components.Children[1] is not Border cableCard ||
                        Grid.GetColumn(microsoftCard) >= Grid.GetColumn(cableCard) || Math.Abs(microsoftCard.ActualWidth - cableCard.ActualWidth) > 1)
                        throw new InvalidOperationException("About must group two equal-width component cards below the project");
                    if (projectCard.Child is not Grid projectLayout || projectLayout.Children.Count != 2 ||
                        projectLayout.Children[0] is not Image { Source: BitmapSource { PixelWidth: > 0 } } logo ||
                        logo.TranslatePoint(new Point(logo.ActualWidth, 0), projectLayout).X >=
                        projectLayout.Children[1].TranslatePoint(new Point(), projectLayout).X)
                        throw new InvalidOperationException("The project logo must sit to the left of the project details");
                    Theme.Apply(aboutWindow, false); aboutWindow.UpdateLayout(); Preview(aboutWindow, "about-light");
                    Theme.Apply(aboutWindow, true); aboutWindow.UpdateLayout(); Preview(aboutWindow, "about-dark");
                    aboutWindow.Close();
                    updating = true; driver.IsChecked = true; driver.IsChecked = false; updating = false;
                    Application.Current.Shutdown(0);
                }
                catch (Exception error) { Console.Error.WriteLine(error); Application.Current.Shutdown(3); }
            }
            else { await Execute(1); timer.Start(); }
        };
    }

    private static TextBlock Label(string key, double size, FontWeight weight, bool secondary = false) => new()
    { Text = Core.Text(key), FontSize = size, FontWeight = weight, TextWrapping = TextWrapping.Wrap, Opacity = secondary ? .65 : 1, Margin = new Thickness(0, 0, 0, 4) };

    private static TextBlock PanelHint()
    {
        var hint = Label("manager.panel.hint", 12, FontWeights.Normal, true);
        hint.Margin = new Thickness(0);
        System.Windows.Automation.AutomationProperties.SetName(hint, hint.Text);
        hint.ToolTip = hint.Text;
        var font = new FontFamily("Segoe Fluent Icons");
        var glyphs = new[] { '\uF4C3', '\uE752' };
        if (!new Typeface(font, FontStyles.Normal, FontWeights.Normal, FontStretches.Normal).TryGetGlyphTypeface(out var typeface) ||
            glyphs.Any(glyph => !typeface.CharacterToGlyphMap.ContainsKey(glyph))) return hint;
        hint.Text = string.Empty;
        foreach (var part in Regex.Split(Core.Text("manager.panel.hint.icons"), @"(\{[012]\})"))
        {
            if (part == "{1}")
            {
                var windows = new System.Windows.Shapes.Path
                {
                    Width = 11, Height = 11, Margin = new Thickness(1, 0, 1, 0),
                    Data = Geometry.Parse("M0,0H5V5H0Z M6,0H11V5H6Z M0,6H5V11H0Z M6,6H11V11H6Z")
                };
                windows.SetResourceReference(System.Windows.Shapes.Shape.FillProperty, "TextBrush");
                hint.Inlines.Add(new InlineUIContainer(windows) { BaselineAlignment = BaselineAlignment.Center });
            }
            else if (part == "{0}" || part == "{2}")
                hint.Inlines.Add(new Run(glyphs[part == "{0}" ? 0 : 1].ToString())
                { FontFamily = font, FontSize = 14, BaselineAlignment = BaselineAlignment.Center });
            else hint.Inlines.Add(new Run(part));
        }
        return hint;
    }

    private void Preview(string theme)
    {
        Preview(this, "manager-" + theme);
    }

    private static void Preview(FrameworkElement window, string name)
    {
        var image = new RenderTargetBitmap((int)window.ActualWidth, (int)window.ActualHeight, 96, 96, PixelFormats.Pbgra32);
        image.Render(window);
        var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(image));
        using var stream = System.IO.File.Create(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, $"{name.Replace("manager-", "manager-preview-")}.png"));
        encoder.Save(stream);
    }

    private static Border Card(UIElement content, double padding, double? verticalPadding = null)
    {
        var card = new Border { Child = content, CornerRadius = new CornerRadius(8), Padding = new Thickness(padding, verticalPadding ?? padding, padding, verticalPadding ?? padding), Margin = new Thickness(0, 0, 0, 8), BorderThickness = new Thickness(1) };
        card.SetResourceReference(Border.BackgroundProperty, "CardBackgroundFillColorDefaultBrush");
        card.SetResourceReference(Border.BorderBrushProperty, "CardStrokeColorDefaultBrush");
        return card;
    }

    private static Border Setting(string glyph, string title, string hint, CheckBox toggle)
    {
        var row = new Grid(); row.ColumnDefinitions.Add(new() { Width = GridLength.Auto }); row.ColumnDefinitions.Add(new()); row.ColumnDefinitions.Add(new() { Width = GridLength.Auto });
        var icon = new TextBlock { Text = glyph, FontFamily = new FontFamily("Segoe Fluent Icons"), FontSize = 22, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 16, 0) }; row.Children.Add(icon);
        var labels = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 16, 0) }; labels.Children.Add(Label(title, 14, FontWeights.Normal));
        var description = Label(hint, 12, FontWeights.Normal, true); description.Margin = new Thickness(0); labels.Children.Add(description); Grid.SetColumn(labels, 1); row.Children.Add(labels);
        toggle.VerticalAlignment = VerticalAlignment.Center; toggle.SetResourceReference(StyleProperty, "ToggleSwitchCheckBoxStyle"); System.Windows.Automation.AutomationProperties.SetName(toggle, Core.Text(title)); Grid.SetColumn(toggle, 2); row.Children.Add(toggle);
        return Card(row, 14);
    }

    private bool Confirm(string title, string message)
    {
        if (busy || modal) return false;
        modal = true;
        try { return MessageBox.Show(this, Core.Text(message), Core.Text(title), MessageBoxButton.OKCancel, MessageBoxImage.Warning, MessageBoxResult.Cancel) == MessageBoxResult.OK; }
        finally { modal = false; }
    }

    private async Task Update()
    {
        if (busy || modal || test || stateRefresh != null) return;
        stateRefresh = PollState();
        try { await stateRefresh; }
        finally { stateRefresh = null; }
    }

    private async Task PollState()
    {
        try { await ReadState(passive: true); }
        catch (Exception error) { if (!busy) Error(error.Message); }
    }

    private async Task ReadState(bool passive = false)
    {
        var values = new int[6];
        if (await Task.Run(() => Core.VmexManagerQuery(values, values.Length)) != 0) throw new InvalidOperationException(Core.Text("manager.unresponsive"));
        if (passive && busy) return;
        Present(values);
        await ReadDriverState(passive);
    }

    private async Task ReadDriverState(bool passive = false)
    {
        var driverValues = new int[5];
        if (await Task.Run(() => Core.VmexRecordingDriverQuery(driverValues, driverValues.Length)) != 0) throw new InvalidOperationException(Core.Text("driver.query.failed"));
        if (!passive || !busy) PresentDriver(driverValues);
    }

    private void PresentDriver(int[] values)
    {
        bool restartRequired = values[4] != 0;
        var state = Core.Text(values[0] == 0 ? "driver.absent" : values[1] != 0 ? "driver.ready" : "driver.notStarted");
        driverStatus.Text = Core.Text("driver.title") + " · " + state;
        if (values[3] != 0) driverStatus.Text += " · " + Core.Text("driver.problem") + " " + values[3];
        if (values[0] == 0 && values[2] == 0) driverStatus.Text += " · " + Core.Text("driver.package.missing");
        installDriver.Visibility = values[0] == 0 ? Visibility.Visible : Visibility.Collapsed;
        uninstallDriver.Visibility = values[0] != 0 ? Visibility.Visible : Visibility.Collapsed;
        driverRestart.Visibility = restartRequired ? Visibility.Visible : Visibility.Collapsed;
        driverHint.Visibility = restartRequired ? Visibility.Collapsed : Visibility.Visible;
        installDriver.IsEnabled = !restartRequired && values[0] == 0 && values[2] != 0;
        uninstallDriver.IsEnabled = !restartRequired && values[0] != 0;
    }

    private void Present(int[] values)
    {
        running = values[0] != 0; updating = true; autostart.IsChecked = values[3] != 0; driver.IsChecked = values[4] != 0; updating = false;
        status.Text = Core.Text(!running ? "manager.state.stopped" : values[2] != 0 ? "manager.panel.ready" : "manager.panel.preparing");
        retryInjection = running && values[2] == 0;
        panel.Content = Core.Text(retryInjection ? "manager.reinject" : "manager.panel");
        panel.ToolTip = retryInjection ? Core.Text("manager.reinject.hint") : null;
        host.Content = Core.Text(running ? "manager.stop" : "manager.start");
    }

    private void Error(string message) => feedback.Text = Core.Text("manager.failed").Replace("{0}", message);

    private void PresentUpdate(bool available)
    {
        updateAvailable = available;
        softwareUpdate.Content = Core.Text(available ? "manager.update.download" : "manager.update.check");
        feedback.Text = Core.Text(available ? "manager.update.available" : "update.current");
    }

    private void ClearReleaseFocus()
    {
        releaseFocusPending = false;
        releaseFocusTimer.Stop();
    }

    private void ReturnFromRelease()
    {
        if (!releaseFocusPending) return;
        ClearReleaseFocus();
        Dispatcher.BeginInvoke(DispatcherPriority.Background, new Action(() =>
        {
            if (!IsLoaded) return;
            Program.BringToForeground(new System.Windows.Interop.WindowInteropHelper(this).Handle);
            Activate();
        }));
    }

    private async Task UpdateSoftware()
    {
        if (busy || modal || test || updateLaunched) return;
        busy = true; body.IsEnabled = false;
        try
        {
            if (stateRefresh is Task pending) await pending;
            var error = new StringBuilder(4096);
            if (!updateAvailable)
            {
                feedback.Text = Core.Text("manager.update.checking");
                int available = 0;
                int result = await Task.Run(() => Core.VmexManagerUpdate(0, out available, error, error.Capacity));
                if (result != 0) { Error(error.ToString()); return; }
                PresentUpdate(available != 0);
                if (!updateAvailable) return;
                releaseFocusPending = true; releaseFocusTimer.Start();
                result = await Task.Run(() => Core.VmexManagerUpdate(1, out _, error, error.Capacity));
                if (result != 0) { ClearReleaseFocus(); Error(error.ToString()); }
                else if (!IsActive) ReturnFromRelease();
            }
            else
            {
                ClearReleaseFocus();
                feedback.Text = Core.Text("manager.update.restarting");
                int result = await Task.Run(() => Core.VmexManagerUpdate(2, out _, error, error.Capacity));
                if (result != 0) { Error(error.ToString()); return; }
                updateLaunched = true;
            }
        }
        catch (Exception error) { ClearReleaseFocus(); Error(error.Message); }
        finally { busy = false; body.IsEnabled = !updateLaunched; }
    }

    private async Task PollUpdate()
    {
        if (busy || !updateLaunched) return;
        busy = true;
        try
        {
            var error = new StringBuilder(4096);
            int running = 0;
            int result = await Task.Run(() => Core.VmexManagerUpdate(3, out running, error, error.Capacity));
            if (result == 0 && running != 0) return;
            updateLaunched = false;
            if (result != 0) Error(error.ToString());
            else PresentUpdate(false);
        }
        catch (Exception error) { updateLaunched = false; Error(error.Message); }
        finally { busy = false; body.IsEnabled = !updateLaunched; }
    }

    private Window CreateAbout()
    {
        var window = new Window
        {
            Owner = this, Title = Core.Text("about.title"), Width = 560, SizeToContent = SizeToContent.Height,
            WindowStartupLocation = WindowStartupLocation.CenterOwner, ResizeMode = ResizeMode.CanResize,
            MinWidth = 420, MinHeight = 360, ShowInTaskbar = false
        };
        window.SetResourceReference(BackgroundProperty, "WindowBrush");
        window.SetResourceReference(ForegroundProperty, "TextBrush");
        window.SetResourceReference(FontFamilyProperty, SystemFonts.MessageFontFamilyKey);
        TextBlock CreateLink(string key)
        {
            var link = new Hyperlink(new Run(Core.Text(key))) { NavigateUri = new Uri(Core.Text(key + ".url")) };
            link.SetResourceReference(TextElement.ForegroundProperty, "AccentBrush");
            link.RequestNavigate += (_, args) =>
            {
                try { Process.Start(new ProcessStartInfo(args.Uri.AbsoluteUri) { UseShellExecute = true }); args.Handled = true; }
                catch (Exception error) { MessageBox.Show(window, Core.Text("manager.failed").Replace("{0}", error.Message), window.Title); }
            };
            var line = new TextBlock { Margin = new Thickness(0, 4, 0, 0), TextWrapping = TextWrapping.Wrap };
            line.Inlines.Add(link);
            return line;
        }

        var content = new StackPanel { Margin = new Thickness(20) };

        var projectDetails = new StackPanel();
        projectDetails.Children.Add(new Viewbox
        {
            Child = Label("manager.title", 26, FontWeights.SemiBold),
            HorizontalAlignment = HorizontalAlignment.Left, StretchDirection = StretchDirection.DownOnly
        });
        var projectVersion = Label("about.version", 13, FontWeights.SemiBold);
        projectVersion.Text = projectVersion.Text.Replace("{0}", Core.Version());
        projectVersion.SetResourceReference(TextBlock.ForegroundProperty, "AccentBrush");
        projectVersion.Margin = new Thickness(0, 0, 0, 12);
        projectDetails.Children.Add(projectVersion);
        projectDetails.Children.Add(Label("about.developer", 14, FontWeights.Normal));
        projectDetails.Children.Add(Label("about.license", 13, FontWeights.Normal, true));
        projectDetails.Children.Add(CreateLink("about.link.project"));
        var projectLayout = new Grid();
        projectLayout.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        projectLayout.ColumnDefinitions.Add(new ColumnDefinition());
        using var logoFile = typeof(ManagerWindow).Assembly.GetManifestResourceStream("Vmex.Manager.ProjectLogo.png")
            ?? throw new InvalidOperationException("Embedded project logo unavailable");
        projectLayout.Children.Add(new Image
        {
            Source = BitmapFrame.Create(logoFile, BitmapCreateOptions.None, BitmapCacheOption.OnLoad),
            Width = 76, Height = 76, VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(0, 0, 20, 0)
        });
        Grid.SetColumn(projectDetails, 1);
        projectLayout.Children.Add(projectDetails);
        var projectCard = Card(projectLayout, 20);
        projectCard.Margin = new Thickness(0, 0, 0, 12);
        content.Children.Add(projectCard);

        var components = new Grid();
        components.ColumnDefinitions.Add(new ColumnDefinition());
        components.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(12) });
        components.ColumnDefinitions.Add(new ColumnDefinition());
        void AddComponent(string component, int column, params string[] links)
        {
            var details = new StackPanel();
            details.Children.Add(Label(component + ".title", 16, FontWeights.SemiBold));
            details.Children.Add(Label(component, 13, FontWeights.Normal, true));
            foreach (var key in links) details.Children.Add(CreateLink(key));
            var card = Card(details, 16);
            card.Margin = new Thickness(0);
            Grid.SetColumn(card, column);
            components.Children.Add(card);
        }
        AddComponent("about.microsoft", 0, "about.link.microsoft");
        AddComponent("about.vbcable", 2, "about.link.vbcable", "about.link.vbcable.license");
        var thirdParty = new StackPanel();
        var thirdPartyTitle = Label("about.thirdParty", 13, FontWeights.Normal, true);
        thirdPartyTitle.Margin = new Thickness(0, 0, 0, 10);
        thirdParty.Children.Add(thirdPartyTitle);
        thirdParty.Children.Add(components);
        var thirdPartyCard = Card(thirdParty, 12);
        thirdPartyCard.Margin = new Thickness(0);
        content.Children.Add(thirdPartyCard);
        window.Content = content;
        window.SourceInitialized += (_, _) => Theme.Apply(window);
        return window;
    }

    private void ShowAbout()
    {
        if (busy || modal) return;
        modal = true;
        try { CreateAbout().ShowDialog(); }
        finally { modal = false; }
    }

    private async Task Execute(int action, int value = 0)
    {
        if (busy || modal || test) return;
        busy = true; body.IsEnabled = false; feedback.Text = Core.Text(action == 13 ? "manager.reinjecting" : "manager.working");
        try
        {
            if (stateRefresh is Task pending) await pending;
            var error = new StringBuilder(4096);
            var result = await Task.Run(() => Core.VmexManagerExecute(action, value, error, error.Capacity));
            if (result != 0) Error(error.ToString());
            else
            {
                feedback.Text = error.ToString();
                if (action is 2 or 7)
                {
                    if (action == 7) MessageBox.Show(this, Core.Text("manager.uninstall.done"), Title, MessageBoxButton.OK, MessageBoxImage.Information);
                    busy = false; Close(); return;
                }
                if (action == 9)
                {
                    MessageBox.Show(this, Core.Text("manager.startmenu.done"), Title, MessageBoxButton.OK, MessageBoxImage.Information);
                    return;
                }
            }
            if (action is 10 or 11)
            {
                await ReadDriverState();
                if (driverRestart.Visibility == Visibility.Visible)
                    MessageBox.Show(this, driverRestart.Text, Core.Text("driver.title"), MessageBoxButton.OK, MessageBoxImage.Warning);
            }
            else await ReadState();
        }
        catch (Exception error) { Error(error.Message); }
        finally { busy = false; body.IsEnabled = true; }
    }
}
