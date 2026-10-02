using static Csnz.Launcher.Localizer;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Text.Json;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.IO.Pipes;

namespace Csnz.Launcher;
public static class GamePaths
{
    public static string NormalizeRoot(string input)
    {
        if (string.IsNullOrWhiteSpace(input)) throw Error<InvalidOperationException>("Error.SelectRoot");
        var path = Path.TrimEndingDirectorySeparator(Path.GetFullPath(input.Trim().Trim('"')));
        if (File.Exists(path) && string.Equals(Path.GetFileName(path), "CSOLauncher.exe", StringComparison.OrdinalIgnoreCase)) path = Path.GetDirectoryName(path)!;
        if (string.Equals(Path.GetFileName(path), "Bin", StringComparison.OrdinalIgnoreCase) && File.Exists(Path.Combine(path, "CSOLauncher.exe"))) path = Path.GetDirectoryName(path)!;
        return path;
    }
    public static bool IsGameRoot(string? path) => !string.IsNullOrWhiteSpace(path) && Path.IsPathFullyQualified(path) && File.Exists(Path.Combine(path, "Bin", "CSOLauncher.exe"));
    public static string ResolveRoot(string savedRoot, bool manual, string executableDirectory)
    {
        string nearby = NormalizeRoot(executableDirectory);
        if (manual && IsGameRoot(savedRoot)) return NormalizeRoot(savedRoot);
        if (IsGameRoot(nearby)) return nearby;
        return IsGameRoot(savedRoot) ? NormalizeRoot(savedRoot) : "";
    }

}
public enum ServerState { Stop, Starting, Ready, Stopping }
public record ServerStatus(ServerState State, UiText DetailText, bool Owned)
{ public string Detail => DetailText.ToString(); }

public sealed class ServerManager : IDisposable
{
    private readonly SemaphoreSlim gate = new(1, 1);
    private Process? owned;
    private string? ownedPath;
    public ServerStatus Status { get; private set; } = new(ServerState.Stop, Msg("Server.NotStarted"), false);
    public event Action<ServerStatus>? Changed;
    private void Publish(ServerState state, UiText detail) { Status = new(state, detail, owned is { HasExited: false }); Changed?.Invoke(Status); }
    public async Task EnsureReadyAsync(LauncherSettings settings, CancellationToken ct)
    {
        await gate.WaitAsync(ct);
        try
        {
            Publish(ServerState.Starting, Msg("Server.Checking"));
            if (await CsnzProtocol.ProbeAsync(settings.Host, settings.Port, ct))
            { Publish(ServerState.Ready, owned is { HasExited: false } ? Msg("Server.ReadyOwned") : Msg("Server.ReadyExisting")); return; }
            if (!CsnzProtocol.IsLoopback(settings.Host)) throw Error<InvalidOperationException>("Error.RemoteNotReady");
            if (!settings.StartLocalServer) throw Error<InvalidOperationException>("Error.AutoStartOff");
            string exe = Path.Combine(GamePaths.NormalizeRoot(settings.GameRoot), "Server", "CSNZ_Server.exe");
            if (!File.Exists(exe)) throw Error<FileNotFoundException>("Error.ServerMissing");
            CheckLocalConfig(settings);
            bool existing = ProcessTools.IsRunning(exe, "CSNZ_Server");
            if (owned is { HasExited: false } && !string.Equals(exe, ownedPath, StringComparison.OrdinalIgnoreCase))
                throw Error<InvalidOperationException>("Error.OtherServerOwned");
            if (!existing)
            {
                owned?.Dispose();
                var start = new ProcessStartInfo(exe) { WorkingDirectory = Path.GetDirectoryName(exe)!, UseShellExecute = false, CreateNoWindow = true,
                    WindowStyle = ProcessWindowStyle.Hidden, RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true };
                owned = Process.Start(start) ?? throw Error<IOException>("Error.ServerCreate"); ownedPath = exe;
                ProcessTools.KeepStdinAliveInChild(owned);
                // Never collect account names/chat/credentials from the server console.
                ProcessTools.DiscardOutput(owned);
            }
            Publish(ServerState.Starting, existing ? Msg("Server.WaitExisting") : Msg("Server.Initializing"));
            for (int i = 0; i < 45; i++)
            {
                ct.ThrowIfCancellationRequested();
                if (owned is { HasExited: true }) throw Error<IOException>("Error.ServerExited", owned.ExitCode);
                if (await CsnzProtocol.ProbeAsync(settings.Host, settings.Port, ct))
                { Publish(ServerState.Ready, owned is { HasExited: false } ? Msg("Server.ReadyOwned") : Msg("Server.ReadyExisting")); return; }
                await Task.Delay(600, ct);
            }
            throw Error<TimeoutException>("Error.ServerTimeout");
        }
        catch (Exception e) { Publish(ServerState.Stop, e is OperationCanceledException ? Msg("Server.Canceled") : Describe(e)); throw; }
        finally { gate.Release(); }
    }
    public async Task RefreshAsync(LauncherSettings settings, CancellationToken ct)
    {
        if (!await gate.WaitAsync(0, ct)) return;
        try
        {
            bool online = await CsnzProtocol.ProbeAsync(settings.Host, settings.Port, ct);
            Publish(online ? ServerState.Ready : ServerState.Stop, online ? (owned is { HasExited: false } ? Msg("Server.ReadyOwned") : Msg("Server.ReadyExisting")) : Msg("Server.Retry"));
        }
        finally { gate.Release(); }
    }
    public async Task StopOwnedAsync(CancellationToken ct)
    {
        await gate.WaitAsync(ct);
        try
        {
            if (owned is not { HasExited: false }) throw Error<InvalidOperationException>("Error.NotOwned");
            Publish(ServerState.Stopping, Msg("Server.Stopping"));
            await owned.StandardInput.WriteLineAsync("shutdown"); await owned.StandardInput.FlushAsync(ct);
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct); timeout.CancelAfter(TimeSpan.FromSeconds(15));
            try { await owned.WaitForExitAsync(timeout.Token); }
            catch (OperationCanceledException) { Publish(ServerState.Ready, Msg("Server.KeptAlive")); throw Error<TimeoutException>("Error.StopTimeout"); }
            owned.Dispose(); owned = null; ownedPath = null; Publish(ServerState.Stop, Msg("Server.Stopped"));
        }
        catch (Exception e) when (Status.State == ServerState.Stopping)
        {
            Publish(owned is { HasExited: false } ? ServerState.Ready : ServerState.Stop, Msg("Server.StopFailed", Describe(e)));
            throw;
        }
        finally { gate.Release(); }
    }
    private static void CheckLocalConfig(LauncherSettings s)
    {
        var path = Path.Combine(s.GameRoot, "Server", "ServerConfig.json");
        using var doc = JsonDocument.Parse(File.ReadAllText(path), new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true });
        var root = doc.RootElement;
        int port = 30002;
        if (root.TryGetProperty("Port", out var p)) port = p.ValueKind == JsonValueKind.String ? int.Parse(p.GetString()!) : p.GetInt32();
        bool tls = root.TryGetProperty("SSL", out var ssl) && ssl.GetBoolean();
        if (port != s.Port || tls != s.UseTls) throw Error<InvalidOperationException>("Error.ConfigMismatch", port, Msg(tls ? "Common.On" : "Common.Off"));
    }
    public void Dispose() { owned?.Dispose(); gate.Dispose(); } // no force kill on exit
}

public static class ProcessTools
{
    [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool DuplicateHandle(IntPtr sourceProcess, IntPtr sourceHandle, IntPtr targetProcess, out IntPtr targetHandle, uint access, bool inherit, uint options);
    public static void KeepStdinAliveInChild(Process process)
    {
        // The upstream console reader spins on EOF. Retaining one write handle
        // INSIDE this child keeps getline blocked if the user closes the UI and
        // explicitly leaves the server running. Windows closes it at child exit.
        var stream = process.StandardInput.BaseStream;
        IntPtr handle = stream switch { PipeStream p => p.SafePipeHandle.DangerousGetHandle(), FileStream f => f.SafeFileHandle.DangerousGetHandle(), _ => IntPtr.Zero };
        if (handle == IntPtr.Zero || !DuplicateHandle(GetCurrentProcess(), handle, process.Handle, out _, 0, false, 2))
            throw Error<IOException>("Error.Stdin");
    }
    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr OpenProcess(uint access, bool inherit, int processId);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern bool QueryFullProcessImageName(IntPtr process, uint flags, System.Text.StringBuilder path, ref int size);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    public static string GetImagePath(int processId)
    {
        var handle = OpenProcess(0x1000, false, processId); // QUERY_LIMITED_INFORMATION works across x86/x64.
        if (handle == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            var path = new System.Text.StringBuilder(32768); int size = path.Capacity;
            if (!QueryFullProcessImageName(handle, 0, path, ref size)) throw new Win32Exception(Marshal.GetLastWin32Error());
            return path.ToString();
        }
        finally { CloseHandle(handle); }
    }
    public static bool IsRunning(string expectedPath, string name)
    {
        foreach (var process in Process.GetProcessesByName(name))
        {
            using (process)
            {
                try { if (string.Equals(Path.GetFullPath(GetImagePath(process.Id)), Path.GetFullPath(expectedPath), StringComparison.OrdinalIgnoreCase)) return true; }
                catch (InvalidOperationException) { }
                catch (Win32Exception) { throw Error<InvalidOperationException>("Error.ProcessCheck", name); }
            }
        }
        return false;
    }
    public static void DiscardOutput(Process process)
    {
        _ = Drain(process.StandardOutput.BaseStream); _ = Drain(process.StandardError.BaseStream);
        static async Task Drain(Stream stream) { try { await stream.CopyToAsync(Stream.Null); } catch (Exception e) when (e is IOException or ObjectDisposedException) { } }
    }
}

public static class GameLauncher
{
    public static string NativeDll => NativeBundle.DllPath;
    public static ProcessStartInfo CreateStartInfo(LauncherSettings settings, string account, string password)
    {
        CsnzProtocol.ValidateCredentials(account, password, false);
        var exe = Path.Combine(settings.GameRoot, "Bin", "CSOLauncher.exe");
        var info = new ProcessStartInfo(exe) { WorkingDirectory = Path.GetDirectoryName(exe)!, UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (var arg in new[] { "-ip", settings.Host, "-port", settings.Port.ToString(),
            "-disableauthui", "-loadmodeeventfromfile", "-loadzbskillfromfile", "-loadzombie5fromfile" }) info.ArgumentList.Add(arg);
        if (settings.UseTls) info.ArgumentList.Add("-usessl");
        // Credentials are not command-line arguments. AuthBridge transfers them
        // only to this child and sends real /login through its existing engine socket.
        return info;
    }
    public static void CheckGame(LauncherSettings s)
    {
        var exe = Path.Combine(s.GameRoot, "Bin", "CSOLauncher.exe");
        if (!File.Exists(exe)) throw Error<FileNotFoundException>("Error.GameMissing");
        if (ProcessTools.IsRunning(exe, "CSOLauncher")) throw Error<InvalidOperationException>("Error.GameRunning");
        foreach (var p in Process.GetProcessesByName("CSNZ_LEGuard")) { p.Dispose(); throw Error<InvalidOperationException>("Error.LegacyPatch"); }
        AuthBridge.Validate(s.GameRoot);
        if (s.EnableNativePatch) NativePatch.Validate(s.GameRoot, NativeDll);
    }
    public static async Task<Process> StartAsync(LauncherSettings settings, string account, string password, Action<UiText> progress, CancellationToken ct)
    {
        CheckGame(settings);
        var start = CreateStartInfo(settings, account, password);
        Process process;
        try { process = Process.Start(start) ?? throw Error<IOException>("Error.GameStart"); }
        finally { start.ArgumentList.Clear(); } // drop startup arguments after process creation (credentials are not arguments)
        ProcessTools.DiscardOutput(process);
        try { await Task.Run(() => AuthBridge.Attach(process, account, password, ct), ct); }
        catch (Exception e) { throw WithText(new InvalidOperationException(Text("Error.AuthAttach", e), e), Msg("Error.AuthAttach", Describe(e))); }
        progress(Msg("Game.AutoLogin", account));
        if (settings.EnableNativePatch)
        {
            try { await Task.Run(() => NativePatch.AttachNewProcess(process, NativeDll, ct), ct); }
            catch (Exception e) { throw WithText(new InvalidOperationException(Text("Error.PatchAttach", e), e), Msg("Error.PatchAttach", Describe(e))); }
            progress(Msg("Game.PatchLoaded"));
        }
        return process;
    }
}
