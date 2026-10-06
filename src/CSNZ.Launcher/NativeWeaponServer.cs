using static Csnz.Launcher.Localizer;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;

namespace Csnz.Launcher;
public sealed class NativeWeaponServer : IDisposable
{
    private Process? owned;
    private string? gameRoot;
    private int[] loadedIds = [];
    private int lobbyPort, dedicatedPort;
    private string advertisedAddress = ""; private bool tls;
    public bool Owned => owned is { HasExited: false };
    public uint RuntimeStatus { get; private set; }
    public int LoadedCount => RuntimeStatus == 3 && Owned ? loadedIds.Length : 0;
    public string LogPath => Path.Combine(WeaponBundle.DirectoryPath, "shared", "logs", $"native-weapons-{owned?.Id ?? 0}.log");
    public bool ReadyFor(LauncherSettings settings) => Owned && (RuntimeStatus is 2 or 3)
        && string.Equals(gameRoot, settings.GameRoot, StringComparison.OrdinalIgnoreCase) && lobbyPort == settings.Port && dedicatedPort == settings.DedicatedWeaponPort
        && advertisedAddress == Multiplayer.AdvertisedAddress(settings) && tls == settings.UseTls
        && loadedIds.SequenceEqual(WeaponBundle.Selected(settings).Select(w => w.Id));
    public async Task EnsureReadyAsync(LauncherSettings settings, CancellationToken ct)
    {
        if (!Multiplayer.NeedsDedicated(settings)) return;
        Multiplayer.Validate(settings); var selected = WeaponBundle.Selected(settings);
        if (Owned)
        {
            if (!ReadyFor(settings)) throw Error<InvalidOperationException>("Weapons.RestartRequired");
            await RefreshAsync(ct); if (RuntimeStatus is not (2 or 3)) throw Error<InvalidOperationException>("Weapons.NativeFailed", LogPath); return;
        }
        WeaponBundle.ValidateGame(settings.GameRoot, selected.Length != 0); WeaponBundle.ValidatePayload();
        if (WeaponAssets.Inspect(settings).Required) throw Error<InvalidOperationException>("Weapons.PrepareFirst");
        if (ProcessTools.IsRunning(Path.Combine(settings.GameRoot, "Bin", "CSOHLDS.exe"), "CSOHLDS")) throw Error<InvalidOperationException>("Weapons.ExistingServer");
        if (settings.DedicatedWeaponPort is < 1 or > 65535 || settings.DedicatedWeaponPort == settings.Port) throw Error<InvalidOperationException>("Error.Port");
        using (var probe = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp)) { probe.ExclusiveAddressUse = true; probe.Bind(new IPEndPoint(IPAddress.Any, settings.DedicatedWeaponPort)); }
        var start = Multiplayer.DedicatedStartInfo(settings);
        owned?.Dispose(); owned = DedicatedConsole.Start(start);
        gameRoot = settings.GameRoot; lobbyPort = settings.Port; dedicatedPort = settings.DedicatedWeaponPort; loadedIds = []; RuntimeStatus = 0; advertisedAddress = Multiplayer.AdvertisedAddress(settings); tls = settings.UseTls;
        try
        {
            bool modulesReady = false;
            for (int attempt = 0; attempt < 300; attempt++)
            {
                ct.ThrowIfCancellationRequested(); if (owned.HasExited) throw Error<IOException>("Weapons.StartFailed");
                owned.Refresh();
                try { var names = owned.Modules.Cast<ProcessModule>().Select(m => m.ModuleName.ToLowerInvariant()).ToHashSet(); modulesReady = names.Contains("mp.dll") && names.Contains("hw.dll"); }
                catch (System.ComponentModel.Win32Exception) { }
                if (modulesReady) break; await Task.Delay(100, ct);
            }
            if (!modulesReady) throw Error<TimeoutException>("Weapons.ModuleTimeout");
            if (selected.Length != 0) await Task.Run(() =>
            {
                // This is the ONLY weapon injection route. The AuthBridge keeps
                // its separate client login role; no weapon DLL enters a client.
                if (!string.Equals(ProcessTools.GetImagePath(owned.Id), Path.Combine(settings.GameRoot, "Bin", "CSOHLDS.exe"), StringComparison.OrdinalIgnoreCase)) throw Error<InvalidOperationException>("Weapons.WrongProcess");
                NativePatch.AttachNewProcess(owned, WeaponBundle.CoreDll, ct, "CSNZWeapons_Api", expectedResult: WeaponBundle.Catalog.Api);
                foreach (var weapon in selected) NativePatch.AttachNewProcess(owned, WeaponBundle.Dll(weapon), ct, "CSNZWeapon_Start", expectedResult: (uint)weapon.Id);
                if (NativePatch.QueryStatus(owned, WeaponBundle.CoreDll, "CSNZWeapons_Start", ct) != 1) throw Error<InvalidOperationException>("Weapons.NativeFailed", LogPath);
            }, ct);
            loadedIds = selected.Select(w => w.Id).ToArray(); await RefreshAsync(ct);
            if (RuntimeStatus is not (2 or 3)) throw Error<InvalidOperationException>("Weapons.NativeFailed", LogPath);
        }
        catch
        {
            // A failed new, owned child is asked to exit. Never kill or inject an
            // already running dedicated server owned by somebody else.
            try { await StopOwnedAsync(CancellationToken.None); } catch { }
            throw;
        }
    }
    public async Task RefreshAsync(CancellationToken ct)
    {
        if (!Owned || loadedIds.Length == 0) { RuntimeStatus = Owned ? 2u : 0u; return; }
        RuntimeStatus = await Task.Run(() => NativePatch.QueryStatus(owned!, WeaponBundle.CoreDll, "CSNZWeapons_Status", ct), ct);
    }
    public async Task StopOwnedAsync(CancellationToken ct)
    {
        if (!Owned) { owned?.Dispose(); owned = null; loadedIds = []; RuntimeStatus = 0; return; }
        if (loadedIds.Length != 0) { try { await Task.Run(() => NativePatch.QueryStatus(owned!, WeaponBundle.CoreDll, "CSNZWeapons_Stop", ct), ct); } catch { } }
        await DedicatedConsole.QuitAsync(owned!, ct);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct); timeout.CancelAfter(TimeSpan.FromSeconds(12));
        try { await owned!.WaitForExitAsync(timeout.Token); }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested) { throw Error<TimeoutException>("Weapons.StopTimeout"); }
        owned.Dispose(); owned = null; loadedIds = []; RuntimeStatus = 0;
    }
    public void Dispose() { owned?.Dispose(); } // Leaving the UI never unloads a live core.
}
