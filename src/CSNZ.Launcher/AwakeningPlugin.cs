using System.Diagnostics;
using System.IO;
using System.Text.Json;
using static Csnz.Launcher.Localizer;

namespace Csnz.Launcher;

// The launcher remains WPF; the complete gameplay repair runs inside the C++ DLL.
// A separate native host handles x64 server injection from this x86 launcher.
public sealed class AwakeningPlugin
{
    private static readonly Lazy<string> Host = new(() => NativeBundle.Extract("ClassAwakening", "0.3.0-native-r1",
        new[] { "AwakeningHost.exe", "ClassAwakening.Server.dll", "AwakeningObserverHost.exe", "ClassAwakening.Observer.dll", "Awakening-NOTICE.txt" }));
    private readonly SemaphoreSlim gate = new(1, 1);
    private readonly SemaphoreSlim observerGate = new(1, 1);
    public UiText Status { get; private set; } = Msg("Plugins.Waiting");
    public bool Active { get; private set; }

    private static async Task<JsonElement> RunAsync(LauncherSettings settings, string command, bool observer, CancellationToken ct)
    {
        string host = observer ? Path.Combine(Path.GetDirectoryName(Host.Value)!, "AwakeningObserverHost.exe") : Host.Value;
        var info = new ProcessStartInfo(host) { UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
            RedirectStandardOutput = true, RedirectStandardError = true, WorkingDirectory = Path.GetDirectoryName(host)! };
        info.ArgumentList.Add("--root"); info.ArgumentList.Add(GamePaths.NormalizeRoot(settings.GameRoot));
        info.ArgumentList.Add("--command"); info.ArgumentList.Add(command);
        using var process = Process.Start(info) ?? throw new IOException("Cannot start the native awakening host.");
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct); timeout.CancelAfter(TimeSpan.FromSeconds(observer ? 75 : 20));
        var output = process.StandardOutput.ReadToEndAsync(timeout.Token); var error = process.StandardError.ReadToEndAsync(timeout.Token);
        try { await process.WaitForExitAsync(timeout.Token); }
        catch { if (!process.HasExited) process.Kill(); throw; } // only this short-lived control client, never the game/server
        using var doc = JsonDocument.Parse(await output); await error;
        if (!doc.RootElement.GetProperty("ok").GetBoolean())
            throw new IOException(doc.RootElement.GetProperty("error").GetString());
        return doc.RootElement.GetProperty("result").Clone();
    }

    public async Task SyncAsync(LauncherSettings settings, CancellationToken ct)
    {
        await gate.WaitAsync(ct);
        try
        {
            if (!Multiplayer.IsHost(settings)) { Active = false; Status = Msg("Plugins.LocalOnly"); return; }
            if (!GamePaths.IsGameRoot(settings.GameRoot)) { Active = false; Status = Msg("Plugins.SelectRoot"); return; }
            var s = await RunAsync(settings, "status", false, ct);
            if (!s.TryGetProperty("pid", out var pid) || pid.ValueKind == JsonValueKind.Null)
            { Active = false; Status = Msg(settings.EnableClassAwakening ? "Plugins.Waiting" : "Plugins.Disabled"); return; }
            bool active = s.GetProperty("enabled").GetBoolean();
            if (active != settings.EnableClassAwakening)
                s = await RunAsync(settings, settings.EnableClassAwakening ? "enable" : "disable", false, ct);
            if (s.TryGetProperty("error", out var err) && err.ValueKind == JsonValueKind.String)
                throw new IOException(err.GetString());
            Active = s.GetProperty("enabled").GetBoolean();
            if (Active && s.GetProperty("version").GetString() != "0.3.0-native")
                throw new IOException("A different Class Awakening version is resident. Restart the local server.");
            Status = Active ? Msg("Plugins.Active", pid.GetInt32()) : Msg("Plugins.Disabled");
        }
        catch (Exception e) when (e is not OperationCanceledException)
        { Active = false; Status = Msg("Plugins.Failed", e.Message); throw; }
        finally { gate.Release(); }
    }

    public async Task SyncObserverAsync(LauncherSettings settings, CancellationToken ct)
    {
        if (!Multiplayer.IsHost(settings) || !GamePaths.IsGameRoot(settings.GameRoot) || !await observerGate.WaitAsync(0, ct)) return;
        try
        {
            var s = await RunAsync(settings, "status", true, ct);
            if (s.GetProperty("pid").ValueKind == JsonValueKind.Null) return;
            bool active = s.GetProperty("enabled").GetBoolean();
            if (active != settings.EnableClassAwakening)
                await RunAsync(settings, settings.EnableClassAwakening ? "enable" : "disable", true, ct);
        }
        finally { observerGate.Release(); }
    }
}
