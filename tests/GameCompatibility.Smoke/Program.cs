using Csnz.Launcher;
using System.Buffers.Binary;
using System.IO;

if (args.Length != 2) throw new Exception("Provide a new compatibility-smoke- output and a read-only game root");
var root = Path.GetFullPath(args[0]); var source = Path.GetFullPath(args[1]);
if (!root.Contains("compatibility-smoke-", StringComparison.OrdinalIgnoreCase) || Directory.Exists(root) || root == source)
    throw new Exception("Use a new isolated compatibility-smoke- directory");
var bin = Path.Combine(root, "Bin"); Directory.CreateDirectory(bin);
int checks = 0;
void Check(bool valid, string label) { if (!valid) throw new Exception("FAIL " + label); checks++; Console.WriteLine("PASS " + label); }
void Denied(Action action, string key, string label)
{
    try { action(); } catch (Exception error) when (error is IOException or InvalidDataException)
    {
        Check(Localizer.Describe(error).Key == key, label); return;
    }
    throw new Exception("FAIL accepted " + label);
}
foreach (var name in new[] { "CSOHLDS.exe", "CSOLauncher.exe", "mp.dll", "hw.dll" })
{
    var original = Path.Combine(source, "Bin", name); var target = Path.Combine(bin, name);
    File.Copy(original, target);
    var bytes = File.ReadAllBytes(target); int pe = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(0x3c));
    BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(pe + 8), 123u); // COFF timestamp
    BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(pe + 24 + 64), 456u); // optional-header checksum
    File.WriteAllBytes(target, bytes);
    using (var overlay = new FileStream(target, FileMode.Append)) overlay.Write("compatibility-fixture-overlay"u8);
    Check(WeaponBundle.Hash(target) != WeaponBundle.Hash(original), "fixture identity differs: " + name);
}
WeaponBundle.ValidateGame(root); WeaponBundle.ValidatePayload();
Check(true, "metadata/overlay changes accepted; own payload hashes and exports still validated");
var local = new LauncherSettings { GameRoot = root, EnabledWeaponIds = [726] };
GameLauncher.CheckGame(local);
Check(true, "auth and weapon launch preflight no longer require original whole-file hashes");

var mpFile = Path.Combine(bin, "mp.dll"); var mp = File.ReadAllBytes(mpFile);
int mpPe = BinaryPrimitives.ReadInt32LittleEndian(mp.AsSpan(0x3c));
var changed = mp.ToArray(); BinaryPrimitives.WriteInt32LittleEndian(changed.AsSpan(mpPe + 24 + 56), 38477824 + 4096);
File.WriteAllBytes(mpFile, changed);
Denied(() => WeaponBundle.ValidateGame(root), "Native.BuildMismatch", "weapon mode still rejects incompatible engine layout");
var plain = new LauncherSettings { GameRoot = root, EnabledWeaponIds = [], NetworkMode = MultiplayerMode.Lan, AdvertisedGameAddress = "192.0.2.123" };
GameLauncher.CheckGame(plain); WeaponBundle.ValidateGame(root, false);
Check(true, "LAN hosting without weapon repair does not enforce weapon engine layout");
File.WriteAllBytes(mpFile, mp);

var server = Path.Combine(bin, "CSOHLDS.exe"); var exe = File.ReadAllBytes(server);
File.Delete(server); // only a file created by this fixture
Denied(() => WeaponBundle.ValidateGame(root), "Native.GameFileMissing", "missing server is distinct from incompatible version");
var join = new LauncherSettings { GameRoot = root, NetworkMode = MultiplayerMode.Join, Host = "192.0.2.123", EnabledWeaponIds = [726] };
GameLauncher.CheckGame(join);
using (var manager = new NativeWeaponServer())
{
    await manager.EnsureReadyAsync(join, CancellationToken.None);
    Check(!manager.Owned, "join mode ignores absent local server and starts no process");
}
local.EnabledWeaponIds = []; GameLauncher.CheckGame(local);
Check(!Multiplayer.NeedsDedicated(local), "local mode without repairs ignores absent dedicated server");
File.WriteAllBytes(server, exe);
var wrong = exe.ToArray(); int exePe = BinaryPrimitives.ReadInt32LittleEndian(wrong.AsSpan(0x3c));
BinaryPrimitives.WriteUInt16LittleEndian(wrong.AsSpan(exePe + 4), 0x8664);
File.WriteAllBytes(server, wrong);
Denied(() => WeaponBundle.ValidateGame(root, false), "Native.GameFormat", "x64 host rejected even with weapons disabled");
File.WriteAllBytes(server, "not a PE"u8.ToArray());
Denied(() => WeaponBundle.ValidateGame(root, false), "Native.GameFormat", "corrupt file is not mistaken for a supported game");
File.WriteAllBytes(server, exe);
foreach (var language in new[] { "zh-CN", "en" })
{
    Localizer.Apply(language);
    Check(!Localizer.Text("Native.BuildMismatch", "mp.dll").Contains("Giga Break"), "generalized compatibility message " + language);
    Check(Localizer.Text("Native.GameFileMissing", server).Contains("CSOHLDS.exe"), "actionable missing-file message " + language);
}
Console.WriteLine($"PASS_OFFLINE_ONLY {checks} compatibility checks; no game, network, credentials or production writes");
