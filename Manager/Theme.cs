using Microsoft.Win32;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;

namespace Vmex.Manager;

internal static class Theme
{
    [DllImport("dwmapi.dll")] private static extern int DwmSetWindowAttribute(nint window, int attribute, ref int value, int size);
    private static bool? lastDark;
    private static bool? lastContrast;
    internal static void Apply(Window? window = null, bool? darkOverride = null)
    {
        bool dark = darkOverride ?? (int)(Registry.GetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "AppsUseLightTheme", 1) ?? 1) == 0;
        bool contrast = SystemParameters.HighContrast;
        if (lastDark != dark || lastContrast != contrast)
        {
            lastDark = dark; lastContrast = contrast;
            var resources = Application.Current.Resources;
            SolidColorBrush Brush(string color) => new((Color)ColorConverter.ConvertFromString(color));
            resources["WindowBrush"] = contrast ? SystemColors.WindowBrush : Brush(dark ? "#202020" : "#F3F3F3");
            resources["TextBrush"] = contrast ? SystemColors.WindowTextBrush : Brush(dark ? "#F5F5F5" : "#202020");
            resources["CardBackgroundFillColorDefaultBrush"] = contrast ? SystemColors.WindowBrush : Brush(dark ? "#2B2B2B" : "#FFFFFF");
            resources["CardStrokeColorDefaultBrush"] = contrast ? SystemColors.WindowTextBrush : Brush(dark ? "#3C3C3C" : "#E2E2E2");
            resources["ControlBrush"] = contrast ? SystemColors.ControlBrush : Brush(dark ? "#383838" : "#FDFDFD");
            resources["HoverBrush"] = contrast ? SystemColors.HighlightBrush : Brush(dark ? "#454545" : "#F1F1F1");
            resources["AccentBrush"] = contrast ? SystemColors.HighlightBrush : Brush(dark ? "#9DCBFF" : "#005FB8");
            resources["OnAccentBrush"] = contrast ? SystemColors.HighlightTextBrush : Brush(dark ? "#111820" : "#FFFFFF");
        }
        if (window != null)
        {
            int flag = dark ? 1 : 0;
            DwmSetWindowAttribute(new WindowInteropHelper(window).Handle, 20, ref flag, sizeof(int));
            var color = ((SolidColorBrush)Application.Current.Resources["WindowBrush"]).Color;
            int captionColor = color.R | color.G << 8 | color.B << 16;
            DwmSetWindowAttribute(new WindowInteropHelper(window).Handle, 35, ref captionColor, sizeof(int));
        }
    }
}
