using System.Runtime.InteropServices;

namespace Csnz.Launcher;

internal static class ShellIcon
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct FileInfo
    {
        public IntPtr Icon;
        public int Index;
        public uint Attributes;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string DisplayName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)] public string TypeName;
    }
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr SHGetFileInfo(string path, uint attributes, out FileInfo info, uint size, uint flags);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern void SHUpdateImage(string path, int iconIndex, uint flags, int imageIndex);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern void SHChangeNotify(uint eventId, uint flags, string item1, IntPtr item2);
    [DllImport("ole32.dll")] private static extern int CoInitializeEx(IntPtr reserved, uint mode);
    [DllImport("ole32.dll")] private static extern void CoUninitialize();
    internal static void Refresh()
    {
        if (Environment.ProcessPath is not { } exe) return;
        // Shell metadata may block. Never delay the login window or server start.
        _ = Task.Run(() =>
        {
            int initialized = CoInitializeEx(IntPtr.Zero, 0);
            if (initialized < 0) return;
            try
            {
                // Our EXE contains one icon group at index 0. UPDATEITEM alone
                // refreshes metadata but can retain an old per-path icon image.
                if (SHGetFileInfo(exe, 0, out var info, (uint)Marshal.SizeOf<FileInfo>(), 0x4000) != IntPtr.Zero)
                    SHUpdateImage(exe, 0, 0, info.Index);
                SHChangeNotify(0x2000, 0x1005, exe, IntPtr.Zero); // UPDATEITEM + PATHW + FLUSH
            }
            finally { CoUninitialize(); }
        });
        // Refresh only this EXE image. No global cache deletion or Explorer restart.
    }
}
