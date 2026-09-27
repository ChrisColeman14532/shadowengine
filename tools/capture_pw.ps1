param(
    [string]$OutPath = (Join-Path (Split-Path -Parent $PSScriptRoot) "screenshot.png")
)

$code = @'
using System;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public class CapPW {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct BITMAPINFOHEADER {
        public int biSize, biWidth, biHeight; public short biPlanes, biBitCount;
        public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant;
    }
    [StructLayout(LayoutKind.Sequential)] public struct BITMAPINFO {
        public BITMAPINFOHEADER bmiHeader; public uint bmiColors;
    }
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr i);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);

    public static StringBuilder Found = new StringBuilder();
    public static IntPtr FoundHwnd = IntPtr.Zero;

    public static bool Callback(IntPtr h, IntPtr i) {
        var sb = new StringBuilder(256);
        GetWindowText(h, sb, 256);
        Found.AppendLine(h + " | [" + sb.ToString() + "]");
        if (sb.ToString().Contains("ShadowEngine")) FoundHwnd = h;
        return true;
    }

    public static string Do(string outPath) {
        EnumWindows(Callback, IntPtr.Zero);
        if (FoundHwnd == IntPtr.Zero) return "NOT FOUND. Windows:\n" + Found.ToString();
        RECT rc; GetWindowRect(FoundHwnd, out rc);
        int w = rc.Right - rc.Left, h2 = rc.Bottom - rc.Top;
        Bitmap bmp = new Bitmap(w, h2, PixelFormat.Format32bppArgb);
        using (Graphics g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            BITMAPINFO bi = new BITMAPINFO();
            bi.bmiHeader.biSize = Marshal.SizeOf(typeof(BITMAPINFOHEADER));
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h2; // top-down
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = 0; // BI_RGB
            bool ok = PrintWindow(FoundHwnd, hdc, 2); // PW_RENDERFULLCONTENT
            g.ReleaseHdc(hdc);
            if (!ok) return "PRINTWINDOW FAILED. Windows:\n" + Found.ToString();
        }
        bmp.Save(outPath, ImageFormat.Png);
        return "SAVED " + outPath + " (" + w + "x" + h2 + ")";
    }
}
'@

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("cappw_" + [guid]::NewGuid().ToString("N") + ".cs")
Set-Content -Path $tmp -Value $code -Encoding UTF8
try {
    Add-Type -Path $tmp -ReferencedAssemblies System.Drawing.dll
} finally {
    Remove-Item $tmp -ErrorAction SilentlyContinue
}

$result = [CapPW]::Do($OutPath)
Write-Output $result
if ($result -like "NOT FOUND*" -or $result -like "PRINTWINDOW*") { exit 1 }
