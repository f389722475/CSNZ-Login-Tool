using Csnz.Launcher;
using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text.Json;

if (args.Length > 0 && args[0] == "--hlds-console-stop") return DedicatedConsole.RunStopHelper(args);
var root = Path.GetFullPath(args[0]); var output = Path.GetFullPath(args[1]);
if (!root.EndsWith("CSNZ0930 test", StringComparison.OrdinalIgnoreCase) || !output.Contains("multiplayer-smoke-")) throw new Exception("Use the isolated CSNZ0930 test installation and a multiplayer-smoke- output");
Directory.CreateDirectory(output); int checks = 0;
void Check(bool valid, string name) { if (!valid) throw new Exception("FAIL " + name); checks++; Console.WriteLine("PASS " + name); }
var configPath = Path.Combine(root, "Server", "ServerConfig.json"); var configBefore = File.ReadAllBytes(configPath);
var assetsBefore = WeaponBundle.Hash(Path.Combine(root, "Data", "fixtrike.nar"));
Check(Multiplayer.Mode(new LauncherSettings()) == MultiplayerMode.Local && Multiplayer.Mode(new LauncherSettings { Host = "192.0.2.10" }) == MultiplayerMode.Join, "legacy endpoints migrate to local / join without losing credentials");
var join = new LauncherSettings { NetworkMode = MultiplayerMode.Join, Host = "192.0.2.10", GameRoot = root, EnabledWeaponIds = WeaponBundle.DefaultIds };
Check(!Multiplayer.NeedsDedicated(join), "remote join never starts or requires a local weapon server");
using (var remote = new NativeWeaponServer()) { await remote.EnsureReadyAsync(join, CancellationToken.None); Check(!remote.Owned, "remote join retains selected host preferences without injection"); }
var joinRoot = Path.Combine(output, "join-client-only"); Directory.CreateDirectory(Path.Combine(joinRoot, "Data"));
join.GameRoot = joinRoot;
var plan = WeaponAssets.Inspect(join);
Check(plan.ConfigPath == null && !plan.ConfigChanged && plan.AssetChanged, "joining requires no local Server directory/configuration");
var backup = WeaponAssets.Apply(plan);
Check(!Directory.Exists(Path.Combine(joinRoot, "Server")) && !File.Exists(Path.Combine(backup, "ServerConfig.json")) && WeaponBundle.Hash(Path.Combine(joinRoot, "Data", "fixtrike.nar")) == WeaponBundle.Asset.Sha256, "join preparation writes only backed-up/known client assets, never server configuration");
foreach (var invalid in new[] { "127.0.0.1", "0.0.0.0", "::1", "example.org", "255.255.255.255", "224.0.0.1" })
{
    bool denied = false; try { Multiplayer.Validate(new LauncherSettings { NetworkMode = MultiplayerMode.Lan, AdvertisedGameAddress = invalid }); } catch (InvalidOperationException) { denied = true; }
    Check(denied, "reject invalid game advertisement " + invalid);
}
var tls = Multiplayer.DedicatedStartInfo(new LauncherSettings { GameRoot = root, NetworkMode = MultiplayerMode.Internet, AdvertisedGameAddress = "192.0.2.123", UseTls = true });
Check(tls.ArgumentList.Contains("-usessl") && tls.ArgumentList.Contains("192.0.2.123") && !tls.ArgumentList.Contains("-hostdomain"), "dedicated TLS argument and concrete IPv4 instead of unsupported hostdomain");

var reports = new List<object>();
foreach (var mode in new[] { MultiplayerMode.Lan, MultiplayerMode.Internet })
{
    using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60)); var ct = deadline.Token;
    var lobby = new TcpListener(IPAddress.Loopback, 0); lobby.Start();
    using var portProbe = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp); portProbe.Bind(new IPEndPoint(IPAddress.Loopback, 0));
    var port = ((IPEndPoint)portProbe.LocalEndPoint!).Port; portProbe.Close();
    var settings = new LauncherSettings { GameRoot = root, NetworkMode = mode, Host = "127.0.0.1", Port = ((IPEndPoint)lobby.LocalEndpoint).Port,
        DedicatedWeaponPort = port, AdvertisedGameAddress = mode == MultiplayerMode.Lan ? "192.168.123.45" : "192.0.2.123", EnabledWeaponIds = mode == MultiplayerMode.Lan ? WeaponBundle.DefaultIds : [] };
    using var manager = new NativeWeaponServer(); TcpClient? client = null; Process? observer = null; bool forced = false;
    try
    {
        var starting = manager.EnsureReadyAsync(settings, ct);
        client = await lobby.AcceptTcpClientAsync(ct); var stream = client.GetStream();
        await stream.WriteAsync(CsnzProtocol.Greeting, ct);
        var body = await CsnzProtocol.ReadFrameAsync(stream, 1, ct);
        Check(body.Length == 9 && body[0] == 81 && body[1] == 0 && BinaryPrimitives.ReadUInt16LittleEndian(body.AsSpan(2, 2)) == port && body.AsSpan(4, 4).SequenceEqual(IPAddress.Parse(settings.AdvertisedGameAddress).GetAddressBytes()), mode + " real CSOHLDS AddServer packet advertises configured game IPv4/UDP port");
        await starting;
        Check(manager.ReadyFor(settings) && manager.RuntimeStatus == 2, mode + " actual native startup / no-map readiness");
        var child = (Process)typeof(NativeWeaponServer).GetField("owned", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(manager)!;
        observer = Process.GetProcessById(child.Id); observer.EnableRaisingEvents = true;
        int pid = child.Id; var stopwatch = Stopwatch.StartNew();
        await manager.StopOwnedAsync(ct); await observer.WaitForExitAsync(ct);
        Check(observer.ExitCode == 0 && !manager.Owned && manager.LoadedCount == 0, mode + " production stop path exits gracefully with code 0, no kill");
        await manager.StopOwnedAsync(ct); Check(!manager.Owned, mode + " repeated stop is harmless");
        reports.Add(new { mode = mode.ToString(), pid, lobbyPort = settings.Port, gamePort = port, advertised = settings.AdvertisedGameAddress, addServer = Convert.ToHexString(body), selected = settings.EnabledWeaponIds.Length, exitCode = observer.ExitCode, shutdownMilliseconds = stopwatch.ElapsedMilliseconds, forced });
    }
    finally
    {
        if (manager.Owned) { try { await manager.StopOwnedAsync(CancellationToken.None); } catch { } }
        if (manager.Owned)
        {
            // Test teardown only; production deliberately has no force-kill path.
            var child = (Process)typeof(NativeWeaponServer).GetField("owned", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(manager)!;
            child.Kill(); await child.WaitForExitAsync(); forced = true;
        }
        observer?.Dispose(); client?.Dispose(); lobby.Stop();
        if (forced) Console.WriteLine("TEST_TEARDOWN_KILL (validation failed)");
    }
}
Check(File.ReadAllBytes(configPath).AsSpan().SequenceEqual(configBefore) && WeaponBundle.Hash(Path.Combine(root, "Data", "fixtrike.nar")) == assetsBefore, "real test installation configuration and assets unchanged");
File.WriteAllText(Path.Combine(output, "result.json"), JsonSerializer.Serialize(new { passed = true, checks, realDedicatedProcesses = reports, accountsUsed = false, lobbyDatabaseOpened = false, externalNetworkUsed = false, gameplayValidated = false, realInternetReachabilityValidated = false }, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine($"PASS {checks} checks; real dedicated startup/announcement/shutdown, no game input/accounts/database; Internet reachability not claimed");
return 0;
