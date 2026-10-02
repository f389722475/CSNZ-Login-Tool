using static Csnz.Launcher.Localizer;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace Csnz.Launcher;

// x86-only loader for Giga Break LE; never replaces game binaries.
public static class NativePatch
{
    private const uint Synchronize = 0x00100000;
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr LoadLibraryEx(string file, IntPtr fileHandle, uint flags);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true, SetLastError = true)] private static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr GetModuleHandle(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern bool GetModuleHandleEx(uint flags, IntPtr address, out IntPtr module);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern uint GetModuleFileName(IntPtr module, StringBuilder path, int capacity);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint allocation, uint protection);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint freeType);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] buffer, UIntPtr size, out UIntPtr written);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr attributes, UIntPtr stack, IntPtr entry, IntPtr argument, uint flags, IntPtr threadId);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool GetExitCodeThread(IntPtr thread, out uint code);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr OpenEvent(uint access, bool inherit, string name);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr VersionDelegate();
    private static InvalidOperationException Failure(UiText message) => Error<InvalidOperationException>("Native.Windows", message, Marshal.GetLastWin32Error());
    public static uint ReadPeTimestamp(string path) => ReadPe(path).Timestamp;
    private static (ushort Machine, uint Timestamp, uint ImageSize) ReadPe(string path)
    {
        using var reader = new BinaryReader(File.OpenRead(path));
        if (reader.ReadUInt16() != 0x5A4D) throw Error<InvalidDataException>("Error.PE");
        reader.BaseStream.Position = 0x3c; int offset = reader.ReadInt32();
        if (offset < 64 || offset + 100 > reader.BaseStream.Length) throw Error<InvalidDataException>("Error.PEBounds");
        reader.BaseStream.Position = offset;
        if (reader.ReadUInt32() != 0x4550) throw Error<InvalidDataException>("Error.PESignature");
        ushort machine = reader.ReadUInt16(); reader.ReadUInt16(); uint timestamp = reader.ReadUInt32();
        reader.BaseStream.Position = offset + 24;
        if (reader.ReadUInt16() != 0x10b) throw Error<InvalidDataException>("Error.PE32");
        reader.BaseStream.Position = offset + 24 + 56; return (machine, timestamp, reader.ReadUInt32());
    }
    public static void Validate(string gameRoot, string dll)
    {
        if (IntPtr.Size != 4) throw Error<InvalidOperationException>("Error.Launcher32");
        void Check(string name, uint stamp, uint size)
        { var pe = ReadPe(Path.Combine(gameRoot, "Bin", name)); if (pe != ((ushort)0x14c, stamp, size)) throw Error<InvalidOperationException>("Native.BuildMismatch", name); }
        Check("CSOLauncher.exe", 1790721054, 811008); Check("mp.dll", 1783989892, 38477824); Check("client.dll", 1783989872, 41570304);
        if (!File.Exists(dll)) throw Error<FileNotFoundException>("Native.Missing", dll);
        var local = LoadLibraryEx(dll, IntPtr.Zero, 0x100 | 0x800);
        if (local == IntPtr.Zero) throw Failure(Msg("Native.Preload"));
        try
        {
            var version = GetProcAddress(local, "GigaBreakLE_Version");
            if (version == IntPtr.Zero || Marshal.PtrToStringAnsi(Marshal.GetDelegateForFunctionPointer<VersionDelegate>(version)()) != "0.7.4-native-r2-partial")
                throw Error<InvalidOperationException>("Native.Version");
            foreach (var export in new[] { "GigaBreakLE_Start", "GigaBreakLE_Status", "GigaBreakLE_Stop" }) if (GetProcAddress(local, export) == IntPtr.Zero) throw Error<InvalidOperationException>("Native.Exports");
        }
        finally { FreeLibrary(local); }
    }
    private static IntPtr RemoteModule(Process process, string path, CancellationToken ct)
    {
        for (int i = 0; i < 40; i++)
        {
            ct.ThrowIfCancellationRequested(); if (process.HasExited) throw Error<InvalidOperationException>("Native.GameExited");
            process.Refresh();
            try { foreach (ProcessModule module in process.Modules) if (string.Equals(Path.GetFullPath(module.FileName), Path.GetFullPath(path), StringComparison.OrdinalIgnoreCase)) return module.BaseAddress; }
            catch (Win32Exception) { }
            Thread.Sleep(100);
        }
        throw Error<InvalidOperationException>("Native.ModuleMissing");
    }
    private static uint RemoteCall(Process process, IntPtr function, IntPtr argument, out bool finished)
    {
        finished = false;
        var thread = CreateRemoteThread(process.Handle, IntPtr.Zero, UIntPtr.Zero, function, argument, 0, IntPtr.Zero);
        if (thread == IntPtr.Zero) { finished = true; throw Failure(Msg("Native.Load")); }
        try
        {
            if (WaitForSingleObject(thread, 60000) != 0) throw Error<TimeoutException>("Native.Timeout");
            finished = true;
            if (!GetExitCodeThread(thread, out var code)) throw Failure(Msg("Native.Result"));
            return code;
        }
        finally { CloseHandle(thread); }
    }
    public static void AttachNewProcess(Process process, string dll, CancellationToken ct, string entryName = "GigaBreakLE_Start", byte[]? argument = null)
    {
        ct.ThrowIfCancellationRequested();
        var local = LoadLibraryEx(dll, IntPtr.Zero, 0x100 | 0x800);
        if (local == IntPtr.Zero) throw Failure(Msg("Native.ComponentPreload"));
        try
        {
            var load = GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryW");
            if (!GetModuleHandleEx(4 | 2, load, out var owner)) throw Failure(Msg("Native.Loader"));
            var path = new StringBuilder(32768); if (GetModuleFileName(owner, path, path.Capacity) == 0) throw Failure(Msg("Native.SystemPath"));
            var remoteOwner = RemoteModule(process, path.ToString(), ct);
            var remoteLoad = IntPtr.Add(remoteOwner, unchecked(load.ToInt32() - owner.ToInt32()));
            var bytes = Encoding.Unicode.GetBytes(Path.GetFullPath(dll) + '\0');
            var memory = VirtualAllocEx(process.Handle, IntPtr.Zero, (UIntPtr)bytes.Length, 0x3000, 4);
            if (memory == IntPtr.Zero) throw Failure(Msg("Native.PathTransfer"));
            bool safeToFree = true;
            uint loaded;
            try
            {
                if (!WriteProcessMemory(process.Handle, memory, bytes, (UIntPtr)bytes.Length, out var written) || written.ToUInt64() != (ulong)bytes.Length) throw Failure(Msg("Native.PathWrite"));
                loaded = RemoteCall(process, remoteLoad, memory, out safeToFree);
            }
            finally { if (safeToFree) VirtualFreeEx(process.Handle, memory, UIntPtr.Zero, 0x8000); }
            if (loaded == 0) throw Error<InvalidOperationException>("Native.LoadRejected");
            var remote = RemoteModule(process, dll, ct);
            if (unchecked((uint)remote.ToInt32()) != loaded) throw Error<InvalidOperationException>("Native.BaseAddress");
            var entry = GetProcAddress(local, entryName); if (entry == IntPtr.Zero) throw Error<InvalidOperationException>("Native.EntryMissing", entryName);
            IntPtr data = IntPtr.Zero; bool argumentReleased = true;
            try
            {
                if (argument is { Length: > 0 })
                {
                    data = VirtualAllocEx(process.Handle, IntPtr.Zero, (UIntPtr)argument.Length, 0x3000, 4);
                    if (data == IntPtr.Zero || !WriteProcessMemory(process.Handle, data, argument, (UIntPtr)argument.Length, out var size) || size.ToUInt64() != (ulong)argument.Length)
                        throw Failure(Msg("Native.Arguments"));
                }
                var result = RemoteCall(process, IntPtr.Add(remote, unchecked(entry.ToInt32() - local.ToInt32())), data, out argumentReleased);
                if (result != 0) throw Error<InvalidOperationException>("Native.InitError", result);
            }
            finally
            {
                if (data != IntPtr.Zero && argumentReleased)
                {
                    if (argument != null) WriteProcessMemory(process.Handle, data, new byte[argument.Length], (UIntPtr)argument.Length, out _);
                    VirtualFreeEx(process.Handle, data, UIntPtr.Zero, 0x8000);
                }
            }
        }
        finally { FreeLibrary(local); }
    }
    internal static uint QueryStatus(Process process, string dll, string export, CancellationToken ct)
    {
        var local = LoadLibraryEx(dll, IntPtr.Zero, 0x100 | 0x800);
        if (local == IntPtr.Zero) throw Failure(Msg("Native.Status"));
        try
        {
            var entry = GetProcAddress(local, export); if (entry == IntPtr.Zero) throw Failure(Msg("Native.StatusExport"));
            var remote = RemoteModule(process, dll, ct);
            return RemoteCall(process, IntPtr.Add(remote, unchecked(entry.ToInt32() - local.ToInt32())), IntPtr.Zero, out _);
        }
        finally { FreeLibrary(local); }
    }
    public static async Task<bool> WaitReadyAsync(Process process, string dll, CancellationToken ct)
    {
        for (int i = 0; i < 305; i++)
        {
            await Task.Delay(1000, ct);
            if (process.HasExited) return false;
            var signal = OpenEvent(Synchronize, false, $"Local\\CSNZ_GigaBreakLE_Native_Result_{process.Id}");
            if (signal == IntPtr.Zero) continue;
            try { if (WaitForSingleObject(signal, 0) != 0) continue; }
            finally { CloseHandle(signal); }
            return await Task.Run(() =>
            {
                var local = LoadLibraryEx(dll, IntPtr.Zero, 0x100 | 0x800);
                if (local == IntPtr.Zero) return false;
                try
                {
                    var entry = GetProcAddress(local, "GigaBreakLE_Status"); if (entry == IntPtr.Zero) return false;
                    var remote = RemoteModule(process, dll, ct);
                    return RemoteCall(process, IntPtr.Add(remote, unchecked(entry.ToInt32() - local.ToInt32())), IntPtr.Zero, out _) == 2;
                }
                finally { FreeLibrary(local); }
            }, ct);
        }
        return false;
    }
}
