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
    private readonly TextBlock status = new(), injection = new(), feedback = new();
    private readonly Button host = new(), refresh = new();
    private readonly Button softwareUpdate = new();
    private readonly TextBlock driverStatus = new();
    private readonly Button installDriver = new(), uninstallDriver = new();
    private readonly CheckBox autostart = new(), driver = new();
    private readonly StackPanel body = new();
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(2) };
    private readonly DispatcherTimer releaseFocusTimer = new() { Interval = TimeSpan.FromSeconds(10) };
    private Task? stateRefresh;
    private bool busy, updating, running, modal;
    private bool updateAvailable, updateLaunched, releaseFocusPending;
    private readonly bool test;

    internal ManagerWindow(bool selfTest, bool updateFailed = false)
    {
        test = selfTest;
        Title = Core.Text("manager.title"); Width = 600; Height = 650;
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
        var layout = new Grid(); layout.RowDefinitions.Add(new() { Height = new GridLength(64) }); layout.RowDefinitions.Add(new());
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
        var scroll = new ScrollViewer { Content = body, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
        scroll.SetResourceReference(StyleProperty, "OverlayScrollViewerStyle");
        Grid.SetRow(scroll, 1); layout.Children.Add(scroll); Content = layout;
        var runtime = new Grid(); runtime.ColumnDefinitions.Add(new()); runtime.ColumnDefinitions.Add(new() { Width = GridLength.Auto });
        var texts = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        status.FontSize = 20; status.FontWeight = FontWeights.Normal; status.Margin = new Thickness(0, 0, 0, 4);
        status.Text = Core.Text("manager.loading"); texts.Children.Add(status);
        injection.Opacity = .65; injection.Margin = new Thickness(0, 0, 0, 4); texts.Children.Add(injection);
        texts.Children.Add(PanelHint());
        runtime.Children.Add(texts);
        var buttons = new Grid { VerticalAlignment = VerticalAlignment.Center };
        buttons.RowDefinitions.Add(new() { Height = GridLength.Auto }); buttons.RowDefinitions.Add(new() { Height = GridLength.Auto });
        var panel = new Button { Content = Core.Text("manager.panel"), Padding = new Thickness(18, 9, 18, 9), Margin = new Thickness(0, 0, 0, 10) };
        panel.SetResourceReference(StyleProperty, "AccentButtonStyle");
        panel.Click += async (_, _) => await Execute(8); buttons.Children.Add(panel);
        host.Content = Core.Text("manager.start"); host.MinWidth = 128; host.Padding = new Thickness(20, 9, 20, 9);
        host.Click += async (_, _) => { if (running && !Confirm("manager.stop", "manager.stop.confirm")) return; await Execute(running ? 2 : 1); };
        refresh.Content = Core.Text("manager.refresh"); refresh.Margin = new Thickness(0, 0, 10, 0); refresh.Padding = new Thickness(18, 9, 18, 9);
        refresh.Click += async (_, _) => await Update();
        var row = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right };
        row.Children.Add(refresh); row.Children.Add(host); Grid.SetRow(row, 1); buttons.Children.Add(row);
        Grid.SetColumn(buttons, 1); runtime.Children.Add(buttons);
        body.Children.Add(Card(runtime, 18, 10));
        body.Children.Add(Label("manager.settings", 14, FontWeights.SemiBold));
        body.Children.Add(Setting("\uE7E8", "page.toggle.autostart", "manager.autostart.hint", autostart));
        body.Children.Add(Setting("\uE7F4", "page.toggle.showDriverName", "manager.driver.hint", driver));
        autostart.Click += async (_, _) => { if (!updating) await Execute(3, autostart.IsChecked == true ? 1 : 0); };
        driver.Click += async (_, _) => { if (!updating) await Execute(4, driver.IsChecked == true ? 1 : 0); };
        body.Children.Add(Label("manager.maintenance", 14, FontWeights.SemiBold));
        var driverCard = new StackPanel();
        var driverRow = new Grid();
        driverRow.ColumnDefinitions.Add(new());
        driverRow.ColumnDefinitions.Add(new() { Width = GridLength.Auto });
        driverStatus.Text = Core.Text("manager.loading");
        driverStatus.TextWrapping = TextWrapping.Wrap;
        driverStatus.VerticalAlignment = VerticalAlignment.Center;
        driverStatus.Margin = new Thickness(0, 0, 16, 0);
        driverRow.Children.Add(driverStatus);
        var driverActions = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        installDriver.Content = Core.Text("driver.install"); installDriver.Padding = new Thickness(16, 8, 16, 8);
        uninstallDriver.Content = Core.Text("driver.uninstall"); uninstallDriver.Padding = new Thickness(16, 8, 16, 8);
        installDriver.IsEnabled = false; uninstallDriver.IsEnabled = false;
        installDriver.Visibility = Visibility.Collapsed; uninstallDriver.Visibility = Visibility.Collapsed;
        installDriver.Click += async (_, _) => await Execute(10);
        uninstallDriver.Click += async (_, _) => { if (Confirm("driver.uninstall", "driver.uninstall.confirm")) await Execute(11); };
        driverActions.Children.Add(installDriver); driverActions.Children.Add(uninstallDriver);
        Grid.SetColumn(driverActions, 1); driverRow.Children.Add(driverActions);
        driverCard.Children.Add(driverRow);
        var driverHint = Label("driver.hint", 12, FontWeights.Normal, true);
        driverHint.Margin = new Thickness(0, 8, 0, 0);
        driverCard.Children.Add(driverHint); body.Children.Add(Card(driverCard, 16, 10));
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
        feedback.TextWrapping = TextWrapping.Wrap; feedback.Margin = new Thickness(0, 12, 0, 0);
        var feedbackStyle = new System.Windows.Style(typeof(TextBlock));
        var emptyFeedback = new Trigger { Property = TextBlock.TextProperty, Value = string.Empty };
        emptyFeedback.Setters.Add(new Setter(VisibilityProperty, Visibility.Collapsed));
        feedbackStyle.Triggers.Add(emptyFeedback); feedback.Style = feedbackStyle;
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
                    if (TryFindResource("ToggleSwitchCheckBoxStyle") is not System.Windows.Style || TryFindResource("AccentButtonStyle") is not System.Windows.Style) throw new InvalidOperationException("Control styles unavailable");
                    Present(new[] { 1, 0, 1, 0, 1, 0 });
                    if (status.Text != Core.Text("manager.state.running")) throw new InvalidOperationException("A transient pipe connection failure must not mark a running host as stopped");
                    Present(new[] { 0, 0, 0, 0, 1, 0 });
                    if (status.Text != Core.Text("manager.state.stopped")) throw new InvalidOperationException("Stopped host status mismatch");
                    Present(new[] { 1, 1, 0, 0, 1, 0 });
                    if (injection.Text != Core.Text("manager.panel.preparing")) throw new InvalidOperationException("Panel readiness status mismatch");
                    Present(new[] { 1, 1, 1, 0, 1, 0 });
                    PresentDriver(new[] { 0, 0, 0, 0 });
                    if (installDriver.IsEnabled || uninstallDriver.IsEnabled) throw new InvalidOperationException("Missing driver package must not offer installation");
                    PresentDriver(new[] { 1, 1, 1, 0 });
                    if (installDriver.Visibility != Visibility.Collapsed || !uninstallDriver.IsEnabled || uninstallDriver.Visibility != Visibility.Visible) throw new InvalidOperationException("Installed driver must only offer removal");
                    PresentDriver(new[] { 0, 0, 1, 0 });
                    if (!installDriver.IsEnabled || installDriver.Visibility != Visibility.Visible || uninstallDriver.Visibility != Visibility.Collapsed) throw new InvalidOperationException("Absent driver must only offer installation");
                    if (WindowStyle != WindowStyle.SingleBorderWindow || !WindowChrome.GetWindowChrome(this).UseAeroCaptionButtons || Title != Core.Text("manager.title")) throw new InvalidOperationException("System window chrome unavailable");
                    if (!WindowChrome.GetIsHitTestVisibleInChrome(about) || Core.About().Contains("VMEX_VERSION_STRING") || !Core.About().Contains("VB-CABLE")) throw new InvalidOperationException("About information or caption hit testing unavailable");
                    updating = true; driver.IsChecked = true; updating = false;
                    Theme.Apply(this, false); UpdateLayout(); Preview("light");
                    Theme.Apply(this, true); UpdateLayout(); Preview("dark"); Theme.Apply(this);
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
                    if (scroll.ScrollableHeight > 1) throw new InvalidOperationException("A short feedback message must fit the default manager height");
                    feedback.Text = string.Empty; UpdateLayout();
                    double contentWidth = body.ActualWidth;
                    Height = 440; UpdateLayout();
                    await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    var scrollbar = (ScrollBar)scroll.Template.FindName("PART_VerticalScrollBar", scroll);
                    if (scrollbar.Visibility != Visibility.Visible || Math.Abs(body.ActualWidth - contentWidth) > 1 || Math.Abs(scroll.ViewportWidth - scroll.ActualWidth) > 1) throw new InvalidOperationException("Overlay scrollbar must not reduce content width");
                    scroll.ScrollToEnd();
                    await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    if (scroll.VerticalOffset <= 0 || Math.Abs(scrollbar.Value - scroll.VerticalOffset) > 1) throw new InvalidOperationException("Overlay scrollbar is not synchronized");
                    if (!frame.HasTransparentCaptionArea) throw new InvalidOperationException("Native caption buttons are covered by the client background");
                    var aboutWindow = CreateAbout();
                    aboutWindow.Show(); await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    if (aboutWindow.Content is not DockPanel aboutContent || aboutContent.Children.Count != 2) throw new InvalidOperationException("About layout unavailable");
                    Theme.Apply(aboutWindow, false); aboutWindow.UpdateLayout(); Preview(aboutWindow, "about-light");
                    Theme.Apply(aboutWindow, true); aboutWindow.UpdateLayout(); Preview(aboutWindow, "about-dark");
                    aboutWindow.Close();
                    Height = 830; scroll.ScrollToTop(); UpdateLayout();
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

    private static void Preview(Window window, string name)
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
        var driverValues = new int[4];
        if (await Task.Run(() => Core.VmexRecordingDriverQuery(driverValues, driverValues.Length)) != 0) throw new InvalidOperationException(Core.Text("driver.query.failed"));
        if (!passive || !busy) PresentDriver(driverValues);
    }

    private void PresentDriver(int[] values)
    {
        var state = Core.Text(values[0] == 0 ? "driver.absent" : values[1] != 0 ? "driver.ready" : "driver.notStarted");
        driverStatus.Text = Core.Text("driver.title") + " · " + state;
        if (values[3] != 0) driverStatus.Text += " · " + Core.Text("driver.problem") + " " + values[3];
        if (values[2] == 0) driverStatus.Text += " · " + Core.Text("driver.package.missing");
        installDriver.Visibility = values[0] == 0 ? Visibility.Visible : Visibility.Collapsed;
        uninstallDriver.Visibility = values[0] != 0 ? Visibility.Visible : Visibility.Collapsed;
        installDriver.IsEnabled = values[0] == 0 && values[2] != 0;
        uninstallDriver.IsEnabled = values[0] != 0 && values[2] != 0;
    }

    private void Present(int[] values)
    {
        running = values[0] != 0; updating = true; autostart.IsChecked = values[3] != 0; driver.IsChecked = values[4] != 0; updating = false;
        status.Text = Core.Text(running ? "manager.state.running" : "manager.state.stopped");
        injection.Text = Core.Text(!running ? "manager.panel.stopped" : values[2] != 0 ? "manager.panel.ready" : "manager.panel.preparing");
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
            Owner = this, Title = Core.Text("about.title"), Width = 560, Height = 600,
            WindowStartupLocation = WindowStartupLocation.CenterOwner, ResizeMode = ResizeMode.CanResize,
            MinWidth = 420, MinHeight = 360, ShowInTaskbar = false
        };
        window.SetResourceReference(BackgroundProperty, "WindowBrush");
        window.SetResourceReference(ForegroundProperty, "TextBrush");
        window.SetResourceReference(FontFamilyProperty, SystemFonts.MessageFontFamilyKey);
        var content = new DockPanel { Margin = new Thickness(24) };
        var footer = new StackPanel { Margin = new Thickness(0, 16, 0, 0) };
        Hyperlink CreateLink(string label, string url)
        {
            var link = new Hyperlink(new Run(label)) { NavigateUri = new Uri(url) };
            link.SetResourceReference(TextElement.ForegroundProperty, "AccentBrush");
            link.RequestNavigate += (_, args) =>
            {
                try { Process.Start(new ProcessStartInfo(args.Uri.AbsoluteUri) { UseShellExecute = true }); args.Handled = true; }
                catch (Exception error) { MessageBox.Show(window, Core.Text("manager.failed").Replace("{0}", error.Message), window.Title); }
            };
            return link;
        }
        var line = new TextBlock { Margin = new Thickness(0, 4, 0, 4) };
        line.Inlines.Add(CreateLink(Core.Text("about.link.project"), Core.Text("about.link.project.url")));
        footer.Children.Add(line);
        DockPanel.SetDock(footer, Dock.Bottom); content.Children.Add(footer);
        var paragraph = new Paragraph { Margin = new Thickness(0) };
        var body = Core.About();
        var position = 0;
        foreach (Match match in Regex.Matches(body, @"https://[^\s]+"))
        {
            paragraph.Inlines.Add(new Run(body.Substring(position, match.Index - position)));
            paragraph.Inlines.Add(CreateLink(match.Value, match.Value));
            position = match.Index + match.Length;
        }
        paragraph.Inlines.Add(new Run(body.Substring(position)));
        var information = new RichTextBox
        {
            Document = new FlowDocument(paragraph) { PagePadding = new Thickness(0) },
            IsReadOnly = true, IsDocumentEnabled = true,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto, BorderThickness = new Thickness(0),
            Background = Brushes.Transparent, Padding = new Thickness(0), FontSize = 14
        };
        information.SetResourceReference(ForegroundProperty, "TextBrush");
        content.Children.Add(information); window.Content = content;
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
        busy = true; body.IsEnabled = false; feedback.Text = Core.Text("manager.working");
        try
        {
            if (stateRefresh is Task pending) await pending;
            var error = new StringBuilder(4096);
            var result = await Task.Run(() => Core.VmexManagerExecute(action, value, error, error.Capacity));
            if (result != 0) { Error(error.ToString()); await ReadState(); return; }
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
            await ReadState();
        }
        catch (Exception error) { Error(error.Message); }
        finally { busy = false; body.IsEnabled = true; }
    }
}
