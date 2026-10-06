using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace Csnz.Launcher;

// CSOHLDS uses GetNumberOfConsoleInputEvents/ReadConsoleInputA, not stdin
// bytes. Give only this child a private hidden console; never send global keys.
public static class DedicatedConsole
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct StartupInfo
    {
        public uint Size; public string? Reserved, Desktop, Title;
        public uint X, Y, Width, Height, CharsX, CharsY, Fill, Flags;
        public ushort Show, ReservedSize; public IntPtr ReservedData, Input, Output, Error;
    }
    [StructLayout(LayoutKind.Sequential)] private struct ProcessInfo { public IntPtr Process, Thread; public uint Id, ThreadId; }
    [StructLayout(LayoutKind.Explicit, Size = 20)]
    private struct InputRecord
    {
        [FieldOffset(0)] public ushort Type;
        [FieldOffset(4)] public int Down;
        [FieldOffset(8)] public ushort Repeat;
        [FieldOffset(10)] public ushort VirtualKey;
        [FieldOffset(14)] public char Character;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcess(string application, StringBuilder command, IntPtr processAttributes, IntPtr threadAttributes, bool inherit, uint flags, IntPtr environment, string directory, ref StartupInfo startup, out ProcessInfo process);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool AttachConsole(uint id);
    [DllImport("kernel32.dll")] private static extern bool FreeConsole();
    [DllImport("kernel32.dll", SetLastError = true)] private static extern uint GetConsoleProcessList([Out] uint[] processes, uint count);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern bool WriteConsoleInput(IntPtr console, InputRecord[] records, uint count, out uint written);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);

    public static Process Start(ProcessStartInfo info)
    {
        // Arguments come from validated endpoints; quote Windows command-line
        // syntax correctly, including the executable/root with spaces.
        static string Quote(string value)
        {
            // This legacy engine preserves unnecessary quotes on -ip values.
            if (value.Length > 0 && !value.Any(c => char.IsWhiteSpace(c) || c == '"')) return value;
            var text = new StringBuilder("\""); int slashes = 0;
            foreach (var c in value)
            {
                if (c == '\\') { slashes++; continue; }
                if (c == '"') text.Append('\\', slashes * 2 + 1).Append(c);
                else text.Append('\\', slashes).Append(c);
                slashes = 0;
            }
            return text.Append('\\', slashes * 2).Append('"').ToString();
        }
        var command = new StringBuilder(Quote(info.FileName) + " " + string.Join(" ", info.ArgumentList.Select(Quote)));
        var startup = new StartupInfo { Size = (uint)Marshal.SizeOf<StartupInfo>(), Flags = 1, Show = 0 }; // STARTF_USESHOWWINDOW / SW_HIDE
        if (!CreateProcess(info.FileName, command, IntPtr.Zero, IntPtr.Zero, false, 0x10, IntPtr.Zero, info.WorkingDirectory, ref startup, out var child)) throw new Win32Exception(Marshal.GetLastWin32Error()); // CREATE_NEW_CONSOLE
        try { return Process.GetProcessById((int)child.Id); }
        finally { CloseHandle(child.Thread); CloseHandle(child.Process); }
    }

    public static async Task QuitAsync(Process child, CancellationToken ct)
    {
        if (child.HasExited) return;
        var info = new ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden };
        foreach (var arg in new[] { "--hlds-console-stop", child.Id.ToString(), child.StartTime.ToUniversalTime().Ticks.ToString(), ProcessTools.GetImagePath(child.Id) }) info.ArgumentList.Add(arg);
        using var helper = Process.Start(info) ?? throw new IOException("Cannot start dedicated console control.");
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct); timeout.CancelAfter(TimeSpan.FromSeconds(5));
        try { await helper.WaitForExitAsync(timeout.Token); }
        catch { if (!helper.HasExited) helper.Kill(); throw; } // only the short-lived helper, never CSOHLDS
        if (helper.ExitCode != 0 && !child.HasExited) throw new IOException("Dedicated console control failed: " + helper.ExitCode);
    }

    // Helper mode runs before the UI/singleton/settings. PID reuse and an
    // unexpected/shared console fail closed. No arbitrary command is accepted.
    public static int RunStopHelper(string[] args)
    {
        if (args.Length != 4 || !int.TryParse(args[1], out var pid) || !long.TryParse(args[2], out var ticks)) return 2;
        try
        {
            using var child = Process.GetProcessById(pid);
            if (child.StartTime.ToUniversalTime().Ticks != ticks || !string.Equals(ProcessTools.GetImagePath(pid), args[3], StringComparison.OrdinalIgnoreCase) || !string.Equals(Path.GetFileName(args[3]), "CSOHLDS.exe", StringComparison.OrdinalIgnoreCase)) return 3;
            FreeConsole(); if (!AttachConsole((uint)pid)) return 4;
            try
            {
                var processes = new uint[8]; uint count = GetConsoleProcessList(processes, 8);
                if (count != 2 || !processes.Take((int)count).ToHashSet().SetEquals(new[] { (uint)pid, (uint)Environment.ProcessId })) return 5;
                var console = CreateFile("CONIN$", 0x40000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
                if (console == new IntPtr(-1)) return 6;
                try
                {
                    var records = "quit\r".SelectMany(c => new[] { true, false }.Select(down => new InputRecord { Type = 1, Down = down ? 1 : 0, Repeat = 1, VirtualKey = c == '\r' ? (ushort)13 : (ushort)char.ToUpperInvariant(c), Character = c })).ToArray();
                    return WriteConsoleInput(console, records, (uint)records.Length, out var written) && written == records.Length ? 0 : 7;
                }
                finally { CloseHandle(console); }
            }
            finally { FreeConsole(); }
        }
        catch (ArgumentException) { return 0; } // already exited
        catch { return 8; }
    }
}
