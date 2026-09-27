param(
    [string]$OutPath = (Join-Path (Split-Path -Parent $PSScriptRoot) "screenshot.png"),
    [string]$TitlePattern = "ShadowEngine"
)

$code = @'
using System;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public class Cap3 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr i);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr d);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr h);
    [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr h);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleBitmap(IntPtr h, int w, int h2);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr h);
    [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr h, IntPtr o);
    [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr hdc, int x, int y, int w, int h, IntPtr hdcSrc, int sx, int sy, uint rop);

    // Fallback screen capture via raw GDI BitBlt (used when GDI+
    // CopyFromScreen fails, e.g. on DPI-virtualized sessions).
    static Bitmap CaptureViaBitBlt(int x, int y, int w, int h) {
        IntPtr screenDC = GetDC(IntPtr.Zero);
        if (screenDC == IntPtr.Zero) return null;
        IntPtr memDC = CreateCompatibleDC(screenDC);
        IntPtr bmpH = CreateCompatibleBitmap(screenDC, w, h);
        IntPtr old = SelectObject(memDC, bmpH);
        bool ok = BitBlt(memDC, 0, 0, w, h, screenDC, x, y, 0x00CC0020); // SRCCOPY
        SelectObject(memDC, old);
        ReleaseDC(IntPtr.Zero, screenDC);
        if (!ok) { DeleteObject(bmpH); DeleteDC(memDC); return null; }
        Bitmap managed = Image.FromHbitmap(bmpH); // copies pixels
        DeleteObject(bmpH);
        DeleteDC(memDC);
        return managed;
    }

    public static StringBuilder Found = new StringBuilder();
    public static IntPtr FoundHwnd = IntPtr.Zero;

    public static bool Callback(IntPtr h, IntPtr i) {
        var sb = new StringBuilder(256);
        GetWindowText(h, sb, 256);
        Found.AppendLine(h + " | [" + sb.ToString() + "]");
        if (sb.ToString().Contains("__PATTERN__")) FoundHwnd = h;
        return true;
    }

    public static string Do(string outPath) {
        // Match the window's coordinate space (Per-Monitor v2 DPI aware),
        // otherwise GetWindowRect returns virtualized coordinates and the
        // screen copy can fail or capture the wrong region.
        SetProcessDpiAwarenessContext(new IntPtr(-4)); // PER_MONITOR_AWARE_V2
        EnumWindows(Callback, IntPtr.Zero);
        if (FoundHwnd == IntPtr.Zero) return "NOT FOUND. Windows:\n" + Found.ToString();
        ShowWindow(FoundHwnd, 9);
        SetForegroundWindow(FoundHwnd);
        System.Threading.Thread.Sleep(600);
        RECT rc; GetWindowRect(FoundHwnd, out rc);
        int w = rc.Right - rc.Left, h2 = rc.Bottom - rc.Top;
        try {
            Bitmap bmp = new Bitmap(w, h2);
            using (Graphics g = Graphics.FromImage(bmp)) g.CopyFromScreen(rc.Left, rc.Top, 0, 0, new Size(w, h2));
            bmp.Save(outPath, ImageFormat.Png);
            return "SAVED " + outPath + " (" + w + "x" + h2 + ")";
        } catch (Exception e1) {
            Bitmap bmp2 = CaptureViaBitBlt(rc.Left, rc.Top, w, h2);
            if (bmp2 != null) {
                bmp2.Save(outPath, ImageFormat.Png);
                return "SAVED (bitblt) " + outPath + " (" + w + "x" + h2 + ")";
            }
            return "CAPTURE FAILED: " + e1.Message;
        }
    }
}
'@

$code = $code.Replace("__PATTERN__", $TitlePattern)
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("cap3_" + [guid]::NewGuid().ToString("N") + ".cs")
Set-Content -Path $tmp -Value $code -Encoding UTF8
try {
    Add-Type -Path $tmp -ReferencedAssemblies System.Drawing.dll
} finally {
    Remove-Item $tmp -ErrorAction SilentlyContinue
}

$result = [Cap3]::Do($OutPath)
Write-Output $result
if ($result -like "NOT FOUND*") { exit 1 }
