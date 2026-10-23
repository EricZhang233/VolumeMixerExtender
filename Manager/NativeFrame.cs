using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;

namespace Vmex.Manager;

internal sealed class NativeFrame
{
    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRect { internal int Left, Top, Right, Bottom; }
    [DllImport("dwmapi.dll")] private static extern int DwmGetWindowAttribute(nint window, int attribute, out NativeRect bounds, int size);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(nint window, out NativeRect bounds);
    private readonly Window window;
    private readonly Border surface;
    internal bool HasTransparentCaptionArea { get; private set; }

    internal NativeFrame(Window window, Border surface)
    {
        this.window = window; this.surface = surface;
        surface.SizeChanged += (_, _) => Update();
        window.ContentRendered += (_, _) => Update();
        window.StateChanged += (_, _) => Schedule();
        window.SourceInitialized += (_, _) => HwndSource.FromHwnd(new WindowInteropHelper(window).Handle).AddHook(WindowMessage);
    }

    private void Schedule() => window.Dispatcher.BeginInvoke(new Action(Update), DispatcherPriority.Loaded);

    private nint WindowMessage(nint hwnd, int message, nint wParam, nint lParam, ref bool handled)
    {
        if (message is 0x02E0 or 0x031E or 0x0320 or 0x0047) Schedule();
        return IntPtr.Zero;
    }

    private void Update()
    {
        if (surface.ActualWidth <= 0 || surface.ActualHeight <= 0 || window.WindowState == WindowState.Minimized) return;
        var handle = new WindowInteropHelper(window).Handle;
        if (DwmGetWindowAttribute(handle, 5, out var caption, Marshal.SizeOf<NativeRect>()) != 0 || !GetWindowRect(handle, out var bounds) || caption.Right <= caption.Left || caption.Bottom <= caption.Top) return;
        var topLeft = surface.PointFromScreen(new Point(bounds.Left + caption.Left, bounds.Top + caption.Top));
        var bottomRight = surface.PointFromScreen(new Point(bounds.Left + caption.Right, bounds.Top + caption.Bottom));
        var captionArea = new Rect(topLeft, bottomRight);
        surface.Clip = new CombinedGeometry(GeometryCombineMode.Exclude, new RectangleGeometry(new Rect(0, 0, surface.ActualWidth, surface.ActualHeight)), new RectangleGeometry(captionArea));
        HasTransparentCaptionArea = !surface.Clip.FillContains(new Point(captionArea.Left + captionArea.Width / 2, captionArea.Top + captionArea.Height / 2));
    }
}
