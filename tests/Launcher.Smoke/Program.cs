using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using Csnz.Launcher;

var root = Path.GetFullPath(args[0]); // explicitly isolated server root; never point at production accounts
if (!root.Contains("server-isolated", StringComparison.OrdinalIgnoreCase)) throw new Exception("Refusing non-isolated test server");
int passed = 0;
void Check(bool ok, string name) { if (!ok) throw new Exception("FAIL: " + name); Console.WriteLine("PASS: " + name); passed++; }
var binding = "synthetic-test-only";
var protectedValue = SecretStore.Protect("TestPass9!", binding);
Check(!protectedValue.Contains("TestPass9!", StringComparison.Ordinal) && SecretStore.Unprotect(protectedValue, binding) == "TestPass9!", "DPAPI current-user roundtrip");
bool denied = false; try { SecretStore.Unprotect(protectedValue, "different-server"); } catch { denied = true; }
Check(denied, "DPAPI rejects changed credential binding");
var settings = new LauncherSettings { GameRoot = root, Port = 31092, RememberPassword = true, Account = "qauser1", ProtectedPassword = protectedValue };
var settingsFile = Path.Combine(root, "smoke-settings.json"); SettingsStore.Save(settings, settingsFile);
Check(!File.ReadAllText(settingsFile).Contains("TestPass9!"), "saved settings contain no plaintext password");
Check(CsnzProtocol.Frame(1, new byte[] { 0, 0 }).SequenceEqual(new byte[] { 85, 1, 2, 0, 0, 0 }), "wire header signature/sequence/length");
Check(!CsnzProtocol.MapRegistrationReply("connected OK").Success, "unknown server reply cannot become registration success");
foreach (var bad in new[] { "abc 123", "\"hello!\"", "12345", "abc\\def" })
{ denied = false; try { CsnzProtocol.ValidateCredentials("qauser1", bad, true); } catch { denied = true; } Check(denied, "reject invalid registration input"); }
var launch = GameLauncher.CreateStartInfo(settings, "qauser1", "TestPass9!");
Check(!launch.UseShellExecute && !launch.ArgumentList.Contains("-username") && !launch.ArgumentList.Contains("-password") && launch.WorkingDirectory.EndsWith("Bin"), "direct EXE arguments, no BAT or shell");
Check(launch.ArgumentList.Contains("-disableauthui"), "automatic game login suppresses duplicate credential dialog");
WeaponBundle.ValidateGame(@"D:\CS\CSNZ0930"); WeaponBundle.ValidatePayload();
Check(true, "current PE profile and native DLL exports/load");

// ServerManager expects GameRoot/Server. Isolated data sits in the provided root,
// so create a contained directory junction using the preparation script before running.
settings.GameRoot = Path.Combine(root, "HarnessGame");
using var manager = new ServerManager();
manager.Changed += s => Console.WriteLine($"STATE: {s.State.ToString().ToLowerInvariant()} owned={s.Owned}");
using var timeout = new CancellationTokenSource(TimeSpan.FromMinutes(2));
try
{
    await manager.EnsureReadyAsync(settings, timeout.Token);
    Check(manager.Status.State == ServerState.Ready && manager.Status.Owned, "real isolated server auto-start/protocol-ready");
    var account = "qa" + DateTimeOffset.UtcNow.ToUnixTimeSeconds();
    var registration = await CsnzProtocol.RegisterAsync(settings, account, "TestPass9!", 1783989872, timeout.Token);
    Check(registration.Success, "real server registration acknowledgement");
    var duplicate = await CsnzProtocol.RegisterAsync(settings, account, "TestPass9!", 1783989872, timeout.Token);
    Check(!duplicate.Success && duplicate.Message.Contains("已存在"), "real duplicate account rejected");
    async Task<byte[]> Login(string password)
    {
        using var tcp = new TcpClient { NoDelay = true }; await tcp.ConnectAsync("127.0.0.1", 31092, timeout.Token);
        var stream = tcp.GetStream(); var banner = new byte[CsnzProtocol.Greeting.Length]; await stream.ReadExactlyAsync(banner, timeout.Token);
        // Protocol dispatch for lobby commands is also available to unauthenticated sockets.
        var command = Encoding.ASCII.GetBytes("/login " + account + " " + password + "\0");
        var body = new byte[2 + command.Length]; body[0] = 67; body[1] = 1; command.CopyTo(body, 2);
        await stream.WriteAsync(CsnzProtocol.Frame(1, body), timeout.Token);
        return await CsnzProtocol.ReadFrameAsync(stream, 1, timeout.Token);
    }
    var wrong = await Login("WrongPass9!");
    Check(wrong.Length > 3 && wrong[0] == 67 && wrong[1] == 10 && Encoding.ASCII.GetString(wrong).Contains("Wrong password or username"), "real wrong password rejected");
    var correct = await Login("TestPass9!");
    Check(correct.Length >= 2 && correct[0] == 1 && correct[1] == 0, "newly registered account accepted by real server");
    await Task.Delay(400, timeout.Token);
    await manager.StopOwnedAsync(timeout.Token);
    Check(manager.Status.State == ServerState.Stop && !manager.Status.Owned, "owned server graceful shutdown");
    Console.WriteLine($"SMOKE PASS: {passed} checks. No production account database accessed by test.");
}
finally
{
    if (manager.Status.Owned) { try { await manager.StopOwnedAsync(CancellationToken.None); } catch { Console.WriteLine("Isolated server cleanup needs manual attention"); } }
}
