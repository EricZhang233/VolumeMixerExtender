using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using System.Windows.Shell;

namespace Vmex.Manager;

internal static class Core
{
    private const string Dll = "vmex_core.dll";
    [DllImport(Dll, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)] internal static extern int VmexManagerInitialize(StringBuilder error, int capacity);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] internal static extern void VmexManagerShutdown();
    [DllImport(Dll, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)] private static extern void VmexManagerText(string key, StringBuilder text, int capacity);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] internal static extern int VmexManagerQuery([Out] int[] values, int count);
    [DllImport(Dll, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)] internal static extern int VmexManagerExecute(int action, int value, StringBuilder error, int capacity);
    internal static string Text(string key) { var text = new StringBuilder(4096); VmexManagerText(key, text, text.Capacity); return text.ToString(); }
}

internal static class Program
{
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(nint hwnd);
    [DllImport("user32.dll")] private static extern bool ShowWindow(nint hwnd, int command);
    [STAThread]
    private static int Main(string[] args)
    {
        bool test = args.Contains("--self-test");
        using var mutex = new Mutex(true, $"Local\\VmExt.Manager.S{Process.GetCurrentProcess().SessionId}{(test ? ".SelfTest" : "")}", out bool first);
        if (!first)
        {
            foreach (var process in Process.GetProcessesByName("vmex_manager"))
                if (process.Id != Process.GetCurrentProcess().Id && process.SessionId == Process.GetCurrentProcess().SessionId && process.MainWindowHandle != IntPtr.Zero)
                { ShowWindow(process.MainWindowHandle, 9); SetForegroundWindow(process.MainWindowHandle); break; }
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
            var window = new ManagerWindow(test);
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
    private readonly CheckBox autostart = new(), driver = new();
    private readonly StackPanel body = new() { Margin = new Thickness(32, 24, 32, 24) };
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(2) };
    private bool busy, updating, running, modal;
    private readonly bool test;

    internal ManagerWindow(bool selfTest)
    {
        test = selfTest;
        Title = Core.Text("manager.title"); Width = 760; Height = 830; MinWidth = 620; MinHeight = 600;
        WindowStyle = WindowStyle.SingleBorderWindow;
        WindowChrome.SetWindowChrome(this, new WindowChrome { CaptionHeight = 72, ResizeBorderThickness = SystemParameters.WindowResizeBorderThickness, GlassFrameThickness = new Thickness(0, 72, 0, 0), UseAeroCaptionButtons = true });
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
        var layout = new Grid(); layout.RowDefinitions.Add(new() { Height = new GridLength(72) }); layout.RowDefinitions.Add(new());
        var surface = new Border { IsHitTestVisible = false };
        surface.SetResourceReference(Border.BackgroundProperty, "WindowBrush");
        Grid.SetRowSpan(surface, 2); layout.Children.Add(surface);
        var frame = new NativeFrame(this, surface);
        var header = new Grid { Margin = new Thickness(32, 0, 160, 0) };
        var heading = Label("manager.title", 23, FontWeights.Normal); heading.Margin = new Thickness(0); heading.VerticalAlignment = VerticalAlignment.Center; header.Children.Add(heading);
        layout.Children.Add(header);
        body.Margin = new Thickness(32, 4, 32, 24);
        var scroll = new ScrollViewer { Content = body, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
        scroll.SetResourceReference(StyleProperty, "OverlayScrollViewerStyle");
        Grid.SetRow(scroll, 1); layout.Children.Add(scroll); Content = layout;
        var runtime = new StackPanel();
        runtime.Children.Add(Label("manager.runtime", 13, FontWeights.SemiBold, true));
        status.FontSize = 21; status.FontWeight = FontWeights.Normal; status.Margin = new Thickness(0, 10, 0, 6);
        status.Text = Core.Text("manager.loading"); runtime.Children.Add(status);
        injection.Opacity = .65; injection.Margin = new Thickness(0, 0, 0, 20); runtime.Children.Add(injection);
        var commands = new StackPanel { Orientation = Orientation.Horizontal };
        host.Content = Core.Text("manager.start"); host.MinWidth = 128; host.Padding = new Thickness(20, 9, 20, 9);
        host.Margin = new Thickness(10, 0, 0, 0);
        host.Click += async (_, _) => { if (running && !Confirm("manager.stop", "manager.stop.confirm")) return; await Execute(running ? 2 : 1); };
        refresh.Content = Core.Text("manager.refresh"); refresh.Margin = new Thickness(10, 0, 0, 0); refresh.Padding = new Thickness(18, 9, 18, 9);
        refresh.Click += async (_, _) => await Update();
        runtime.Children.Add(commands); body.Children.Add(Card(runtime, 24));
        var panel = new Button { Content = Core.Text("manager.panel"), Padding = new Thickness(18, 9, 18, 9) };
        panel.SetResourceReference(StyleProperty, "AccentButtonStyle");
        panel.Click += async (_, _) => await Execute(8);
        commands.Children.Add(panel); commands.Children.Add(host); commands.Children.Add(refresh);
        body.Children.Add(Label("manager.settings", 14, FontWeights.SemiBold));
        body.Children.Add(Setting("\uE7E8", "page.toggle.autostart", "manager.autostart.hint", autostart));
        body.Children.Add(Setting("\uE7F4", "page.toggle.showDriverName", "manager.driver.hint", driver));
        autostart.Click += async (_, _) => { if (!updating) await Execute(3, autostart.IsChecked == true ? 1 : 0); };
        driver.Click += async (_, _) => { if (!updating) await Execute(4, driver.IsChecked == true ? 1 : 0); };
        body.Children.Add(Label("manager.maintenance", 14, FontWeights.SemiBold));
        var links = new StackPanel { Orientation = Orientation.Horizontal };
        foreach (var item in new[] { ("manager.logs", 5), ("manager.project", 6) })
        {
            var button = new Button { Content = Core.Text(item.Item1), Padding = new Thickness(16, 8, 16, 8), Margin = new Thickness(0, 0, 10, 0) };
            button.Click += async (_, _) => await Execute(item.Item2); links.Children.Add(button);
        }
        body.Children.Add(Card(links, 20));
        var uninstall = new StackPanel(); uninstall.Children.Add(Label("manager.uninstall.hint", 13, FontWeights.Normal, true));
        var remove = new Button { Content = Core.Text("manager.uninstall"), Margin = new Thickness(0, 16, 0, 0), Padding = new Thickness(16, 8, 16, 8), HorizontalAlignment = HorizontalAlignment.Left };
        remove.Click += async (_, _) => { if (Confirm("manager.uninstall", "manager.uninstall.confirm")) await Execute(7); };
        uninstall.Children.Add(remove);
        body.Children.Add(Card(new Expander { Header = Core.Text("manager.uninstall"), Content = uninstall, Padding = new Thickness(2) }, 16));
        feedback.TextWrapping = TextWrapping.Wrap; feedback.Margin = new Thickness(0, 12, 0, 0); body.Children.Add(feedback);
        var footer = Label("manager.ready", 12, FontWeights.Normal, true); footer.Margin = new Thickness(0, 20, 0, 0); body.Children.Add(footer);
        timer.Tick += async (_, _) => { Theme.Apply(this); await Update(); };
        Closing += (_, args) => { if (busy || modal) args.Cancel = true; };
        Closed += (_, _) => timer.Stop();
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
                    if (status.Text != Core.Text("cli.value.running")) throw new InvalidOperationException("A transient pipe connection failure must not mark a running host as stopped");
                    Present(new[] { 0, 0, 0, 0, 1, 0 });
                    if (status.Text != Core.Text("cli.value.stopped")) throw new InvalidOperationException("Stopped host status mismatch");
                    Present(new[] { 1, 1, 0, 0, 1, 0 });
                    if (injection.Text != Core.Text("manager.panel.preparing")) throw new InvalidOperationException("Panel readiness status mismatch");
                    Present(new[] { 1, 1, 1, 0, 1, 0 });
                    if (WindowStyle != WindowStyle.SingleBorderWindow || !WindowChrome.GetWindowChrome(this).UseAeroCaptionButtons || Title != Core.Text("manager.title")) throw new InvalidOperationException("System window chrome unavailable");
                    updating = true; driver.IsChecked = true; updating = false;
                    Theme.Apply(this, false); UpdateLayout(); Preview("light");
                    Theme.Apply(this, true); UpdateLayout(); Preview("dark"); Theme.Apply(this);
                    if (ActualWidth < 600 || status.Text.Length == 0 || host.Template is null || autostart.Template is null) throw new InvalidOperationException("WPF layout or resources unavailable");
                    double contentWidth = body.ActualWidth;
                    Height = 600; UpdateLayout();
                    await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    var scrollbar = (ScrollBar)scroll.Template.FindName("PART_VerticalScrollBar", scroll);
                    if (scrollbar.Visibility != Visibility.Visible || Math.Abs(body.ActualWidth - contentWidth) > 1 || Math.Abs(scroll.ViewportWidth - scroll.ActualWidth) > 1) throw new InvalidOperationException("Overlay scrollbar must not reduce content width");
                    scroll.ScrollToEnd();
                    await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                    if (scroll.VerticalOffset <= 0 || Math.Abs(scrollbar.Value - scroll.VerticalOffset) > 1) throw new InvalidOperationException("Overlay scrollbar is not synchronized");
                    if (!frame.HasTransparentCaptionArea) throw new InvalidOperationException("Native caption buttons are covered by the client background");
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
    { Text = Core.Text(key), FontSize = size, FontWeight = weight, TextWrapping = TextWrapping.Wrap, Opacity = secondary ? .65 : 1, Margin = new Thickness(0, 0, 0, 8) };

    private void Preview(string theme)
    {
        var image = new RenderTargetBitmap((int)ActualWidth, (int)ActualHeight, 96, 96, PixelFormats.Pbgra32);
        image.Render(this);
        var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(image));
        using var stream = System.IO.File.Create(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, $"manager-preview-{theme}.png"));
        encoder.Save(stream);
    }

    private static Border Card(UIElement content, double padding) 
    {
        var card = new Border { Child = content, CornerRadius = new CornerRadius(8), Padding = new Thickness(padding), Margin = new Thickness(0, 0, 0, 8), BorderThickness = new Thickness(1) };
        card.SetResourceReference(Border.BackgroundProperty, "CardBackgroundFillColorDefaultBrush");
        card.SetResourceReference(Border.BorderBrushProperty, "CardStrokeColorDefaultBrush");
        return card;
    }

    private static Border Setting(string glyph, string title, string hint, CheckBox toggle)
    {
        var row = new Grid(); row.ColumnDefinitions.Add(new() { Width = GridLength.Auto }); row.ColumnDefinitions.Add(new()); row.ColumnDefinitions.Add(new() { Width = GridLength.Auto });
        var icon = new TextBlock { Text = glyph, FontFamily = new FontFamily("Segoe Fluent Icons"), FontSize = 22, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 20, 0) }; row.Children.Add(icon);
        var labels = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 20, 0) }; labels.Children.Add(Label(title, 14, FontWeights.Normal)); labels.Children.Add(Label(hint, 12, FontWeights.Normal, true)); Grid.SetColumn(labels, 1); row.Children.Add(labels);
        toggle.VerticalAlignment = VerticalAlignment.Center; toggle.SetResourceReference(StyleProperty, "ToggleSwitchCheckBoxStyle"); System.Windows.Automation.AutomationProperties.SetName(toggle, Core.Text(title)); Grid.SetColumn(toggle, 2); row.Children.Add(toggle);
        return Card(row, 20);
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
        if (busy || modal || test) return;
        busy = true;
        try { await ReadState(); }
        catch (Exception error) { Error(error.Message); }
        finally { busy = false; }
    }

    private async Task ReadState()
    {
        var values = new int[6];
        if (await Task.Run(() => Core.VmexManagerQuery(values, values.Length)) != 0) throw new InvalidOperationException(Core.Text("manager.unresponsive"));
        Present(values);
    }

    private void Present(int[] values)
    {
        running = values[0] != 0; updating = true; autostart.IsChecked = values[3] != 0; driver.IsChecked = values[4] != 0; updating = false;
        status.Text = Core.Text(running ? "cli.value.running" : "cli.value.stopped");
        injection.Text = Core.Text(!running ? "manager.panel.stopped" : values[2] != 0 ? "manager.panel.ready" : "manager.panel.preparing");
        host.Content = Core.Text(running ? "manager.stop" : "manager.start");
    }

    private void Error(string message) => feedback.Text = Core.Text("manager.failed").Replace("{0}", message);

    private async Task Execute(int action, int value = 0)
    {
        if (busy || modal || test) return;
        busy = true; body.IsEnabled = false; feedback.Text = Core.Text("manager.working");
        try
        {
            var error = new StringBuilder(4096);
            var result = await Task.Run(() => Core.VmexManagerExecute(action, value, error, error.Capacity));
            if (result != 0) { Error(error.ToString()); await ReadState(); return; }
            feedback.Text = "";
            if (action is 2 or 7)
            {
                if (action == 7) MessageBox.Show(this, Core.Text("manager.uninstall.done"), Title, MessageBoxButton.OK, MessageBoxImage.Information);
                busy = false; Close(); return;
            }
            await ReadState();
        }
        catch (Exception error) { Error(error.Message); }
        finally { busy = false; body.IsEnabled = true; }
    }
}
