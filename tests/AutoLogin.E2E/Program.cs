using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using Csnz.Launcher;

// Actual game on a private copy, fresh private database, synthetic credentials only.
// The transparent observer logs booleans/packet IDs, never credentials or payloads.
var root = Path.GetFullPath(args[0]);
if (!root.Contains("autologin-e2e-", StringComparison.OrdinalIgnoreCase) || !root.EndsWith("Game"))
    throw new InvalidOperationException("Refusing a non-isolated game tree.");
var mode = args[1];
var direct = mode.StartsWith("direct");
var existingCharacter = mode.Contains("lobby");
int baselineLines = 0;
var reportDirectory = Path.Combine(Path.GetDirectoryName(root)!, "runs", DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + mode);
Directory.CreateDirectory(reportDirectory);
var account = "qa" + DateTimeOffset.UtcNow.ToUnixTimeSeconds();
var password = mode.Contains("symbols") ? "@Test-pass!" : "TestPass9!";
var providedPassword = mode.Contains("wrong") ? "WrongPass9!" : password;
var settings = new LauncherSettings { GameRoot = root, Host = "127.0.0.1", Port = 31093, UseTls = false, EnableNativePatch = false };
using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(300));
var ct = deadline.Token;
Process? server = null;
Process? game = null;
var listener = new TcpListener(IPAddress.Loopback, 31094);
int loginCommands = 0;
bool accountMatched = false, passwordMatched = false, accepted = false, rejected = false;
var observed = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
var ids = new List<string>();
try
{
    if (await CsnzProtocol.ProbeAsync("127.0.0.1", 31093, ct)) throw new Exception("Isolated test port is already in use.");
    server = Process.Start(new ProcessStartInfo(Path.Combine(root, "Server", "CSNZ_Server.exe")) {
        WorkingDirectory = Path.Combine(root, "Server"), UseShellExecute = false, CreateNoWindow = true,
        RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true }) ?? throw new Exception("Test server did not start.");
    ProcessTools.DiscardOutput(server);
    for (int attempt = 0; !await CsnzProtocol.ProbeAsync("127.0.0.1", 31093, ct); attempt++)
    { if (server.HasExited || attempt > 60) throw new Exception("Test server not ready."); await Task.Delay(250, ct); }
    var registration = await CsnzProtocol.RegisterAsync(settings, account, password, 1783989872, ct);
    if (!registration.Success) throw new Exception("Synthetic registration failed: " + registration.Message);
    if (existingCharacter)
    {
        using (var bootstrap = new TcpClient { NoDelay = true })
        {
            await bootstrap.ConnectAsync("127.0.0.1", 31093, ct);
            var stream = bootstrap.GetStream(); var banner = new byte[CsnzProtocol.Greeting.Length]; await stream.ReadExactlyAsync(banner, ct);
            var cmd = Encoding.ASCII.GetBytes($"/login {account} {password}\0");
            var body = new byte[cmd.Length + 2]; body[0] = 67; body[1] = 1; cmd.CopyTo(body, 2);
            await stream.WriteAsync(CsnzProtocol.Frame(1, body), ct);
            var reply = await CsnzProtocol.ReadFrameAsync(stream, 1, ct);
            var create = await CsnzProtocol.ReadFrameAsync(stream, 2, ct);
            if (reply[0] != 1 || reply[1] != 0 || create[0] != 6) throw new Exception("Synthetic bootstrap login failed.");
            var name = Encoding.ASCII.GetBytes("T" + account[2..] + "\0");
            var character = new byte[name.Length + 1]; character[0] = 2; name.CopyTo(character, 1);
            await stream.WriteAsync(CsnzProtocol.Frame(2, character), ct);
            bool created = false;
            for (int seq = 3; seq < 240; seq++)
            {
                var packet = await CsnzProtocol.ReadFrameAsync(stream, (byte)seq, ct);
                if (packet[0] == 1) { created = packet[1] == 1; if (!created) throw new Exception("Synthetic character reply: " + packet[1]); }
                if (packet[0] == 69 && created) break;
            }
            if (!created) throw new Exception("Synthetic character creation was not acknowledged.");
        }
        await Task.Delay(800, ct);
        Console.WriteLine("SYNTHETIC_CHARACTER_CREATED");
    }
    baselineLines = File.ReadAllLines(Directory.GetFiles(Path.Combine(root, "Server", "Logs")).OrderByDescending(File.GetLastWriteTimeUtc).First()).Length;
    listener.Start(1);
    var relay = Task.Run(async () =>
    {
        using var client = await listener.AcceptTcpClientAsync(ct);
        using var upstream = new TcpClient { NoDelay = true };
        client.NoDelay = true;
        await upstream.ConnectAsync(IPAddress.Loopback, 31093, ct);
        var fromGame = client.GetStream(); var fromServer = upstream.GetStream();
        var greeting = new byte[CsnzProtocol.Greeting.Length]; await fromServer.ReadExactlyAsync(greeting, ct);
        await fromGame.WriteAsync(greeting, ct);
        async Task Pump(Stream input, Stream output, bool outgoing)
        {
            while (!ct.IsCancellationRequested)
            {
                var header = new byte[4]; await input.ReadExactlyAsync(header, ct);
                var size = BinaryPrimitives.ReadUInt16LittleEndian(header.AsSpan(2));
                if (header[0] != 0x55 || size == 0) throw new InvalidDataException($"Unknown frame header: {Convert.ToHexString(header)}");
                var body = new byte[size]; await input.ReadExactlyAsync(body, ct);
                lock (ids) { if (ids.Count < 250) ids.Add($"{(outgoing ? "C" : "S")}:seq={header[1]},id={body[0]},len={size}"); }
                if (outgoing && size > 9 && body[0] == 67 && body[1] == 1)
                {
                    var command = Encoding.ASCII.GetString(body, 2, size - 2).TrimEnd('\0');
                    if (command.StartsWith("/login ", StringComparison.Ordinal))
                    {
                        Interlocked.Increment(ref loginCommands);
                        var parts = command.Split(' ');
                        accountMatched = parts.Length == 3 && parts[1] == account;
                        passwordMatched = parts.Length == 3 && parts[2] == providedPassword;
                        Console.WriteLine($"AUTO_LOGIN_SENT accountMatch={accountMatched} passwordMatch={passwordMatched}");
                    }
                }
                if (!outgoing && Volatile.Read(ref loginCommands) > 0)
                {
                    if (size >= 2 && body[0] == 1 && body[1] == 0) accepted = true;
                    if (size > 2 && body[0] == 67 && body[1] == 10 && Encoding.ASCII.GetString(body).Contains("Wrong password or username")) rejected = true;
                }
                var packet = new byte[4 + body.Length]; header.CopyTo(packet, 0); body.CopyTo(packet, 4);
                await output.WriteAsync(packet, ct); Array.Clear(packet);
                Array.Clear(body);
                if (rejected || (accepted && (!existingCharacter || ids.Any(x => x.Contains("id=69,"))))) observed.TrySetResult(true);
            }
        }
        try { await await Task.WhenAny(Pump(fromGame, fromServer, true), Pump(fromServer, fromGame, false)); }
        catch (Exception error) { Console.WriteLine("OBSERVER: " + error.Message); }
    }, ct);
    settings.Port = direct ? 31093 : 31094;
    if (mode.Contains("native")) throw new InvalidOperationException("The old client weapon E2E route was removed. Validate server-native weapons through an owned CSOHLDS fixture instead.");
    settings.EnableNativePatch = false; settings.EnabledWeaponIds = [];
    if (args.Length > 2) settings.GameRoot = Path.GetFullPath(args[2]);
    game = await GameLauncher.StartAsync(settings, account, providedPassword, _ => { }, ct);
    Console.WriteLine($"TEST_GAME_PID={game.Id} mode={mode}");
    if (direct)
    {
        int hold = int.TryParse(Environment.GetEnvironmentVariable("CSNZ_TEST_HOLD_SECONDS"), out var requested) ? Math.Clamp(requested, 30, 180) : 30;
        await Task.Delay(TimeSpan.FromSeconds(hold), ct);
        var log = Directory.GetFiles(Path.Combine(root, "Server", "Logs")).OrderByDescending(File.GetLastWriteTimeUtc).First();
        var lines = File.ReadAllLines(log).Skip(baselineLines).ToArray();
        var directReport = new { mode, gameExited = game.HasExited, initialLoginPacket = lines.Any(x => x.Contains("sent login packet")),
            authenticated = lines.Any(x => x.Contains("User logged in") && x.Contains(account)),
            authRejected = lines.Any(x => x.Contains("Login failed")),
            joinedChannel = lines.Any(x => x.Contains("join") && x.Contains(account)),
            nativeReady = false };
        var data = JsonSerializer.Serialize(directReport, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(Path.Combine(reportDirectory, "result.json"), data); Console.WriteLine(data);
        if (game.HasExited) throw new Exception("Game exited during lobby acceptance. Exit code: " + game.ExitCode);
        if (mode.Contains("wrong") ? !directReport.authRejected || directReport.authenticated : !directReport.authenticated || (existingCharacter && !directReport.joinedChannel))
            throw new Exception("Direct game authentication/lobby result did not meet expectations.");
        if (settings.EnableNativePatch && !directReport.nativeReady) throw new Exception("Weapon patch was not ready.");
        Console.WriteLine("DIRECT AUTH PASS"); return;
    }
    var outcome = await Task.WhenAny(observed.Task, Task.Delay(90000, ct), game.WaitForExitAsync(ct), relay);
    bool nativeReady = false;
    var report = new { mode, loginCommands, accountMatched, passwordMatched, accepted, rejected,
        nativePatchEnabled = settings.EnableNativePatch, nativeReady, gameExited = game.HasExited, packets = ids.ToArray() };
    var json = JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true });
    File.WriteAllText(Path.Combine(reportDirectory, "result.json"), json);
    Console.WriteLine(json);
    Console.WriteLine("CAPTURE_WINDOW_NOW: no authentication UI input is needed; this is a synthetic protocol test.");
    await Task.Delay(20000, ct);
    if (loginCommands != 1 || !accountMatched || !passwordMatched || (mode.Contains("wrong") ? !rejected || accepted : !accepted || rejected) || (settings.EnableNativePatch && !nativeReady))
        throw new Exception("Actual-game automatic login did not satisfy the expected protocol result.");
    if (game.HasExited) throw new Exception("Game exited before lobby acceptance.");
    if (existingCharacter && !mode.Contains("wrong") && !new[] {150, 152, 154, 72, 153, 69}.All(id => ids.Any(x => x.StartsWith("S:") && x.Contains($",id={id},"))))
        throw new Exception("Full lobby data did not reach the game (profile, inventory, loadout, shop, channel, update).");
    Console.WriteLine("E2E PASS");
}
finally
{
    // This PID is exclusively this test's disposable game, never a user's running game.
    if (game is { HasExited: false }) { game.Kill(); await game.WaitForExitAsync(); }
    game?.Dispose(); listener.Stop(); deadline.Cancel();
    if (server is { HasExited: false })
    { await server.StandardInput.WriteLineAsync("shutdown"); await server.StandardInput.FlushAsync(); using var stop = new CancellationTokenSource(15000); await server.WaitForExitAsync(stop.Token); }
    server?.Dispose();
}
