using Csnz.Launcher;
using System.IO;
using System.Text.Json;
using System.Reflection;

var root = Path.GetFullPath(args[0]);
if (!root.Contains("weapon-native-smoke-", StringComparison.OrdinalIgnoreCase) || Directory.Exists(root)) throw new Exception("Use a new isolated weapon-native-smoke- directory");
Directory.CreateDirectory(Path.Combine(root, "Server")); Directory.CreateDirectory(Path.Combine(root, "Data"));
int count = 0;
void Check(bool valid, string name) { if (!valid) throw new Exception("FAIL " + name); count++; Console.WriteLine("PASS " + name); }
Check(WeaponBundle.Catalog.Weapons.Length == 12 && WeaponBundle.Catalog.Weapons.Select(w => w.Id).Distinct().Count() == 12, "12 repaired module IDs; reference retired");
Check(!typeof(WeaponBundle).Assembly.GetManifestResourceNames().Contains("Native.GigaBreakLE.dll"), "old client Giga binary absent");
var enabled = SettingsStore.ResolveGameRoot(new LauncherSettings { EnableNativePatch = true }, root);
var disabled = SettingsStore.ResolveGameRoot(new LauncherSettings { EnableNativePatch = false }, root);
Check(WeaponBundle.Selected(enabled).Length == 12 && WeaponBundle.Selected(disabled).Length == 0, "legacy master preference migrates without enabling an old disabled patch");
var subset = SettingsStore.ResolveGameRoot(new LauncherSettings { EnabledWeaponIds = [726, 597] }, root);
Check(WeaponBundle.Selected(subset).Select(w => w.Id).SequenceEqual(new[] { 726, 597 }), "explicit selection survives migration");
Check(!WeaponBundle.Catalog.Weapons.Any(w => w.Id == 537) && !typeof(WeaponBundle).Assembly.GetManifestResourceNames().Contains("WeaponMods.Arbalest.dll"), "reference omitted from UI/catalog and payload");
Check(WeaponBundle.Selected(new LauncherSettings { EnabledWeaponIds = [537, 591, 726] }).Select(w => w.Id).Order().SequenceEqual(new[] { 591, 726 }), "retired reference preference migrates without losing repairs");
bool denied = false; try { WeaponBundle.Selected(new LauncherSettings { EnabledWeaponIds = [999999] }); } catch (InvalidDataException) { denied = true; }
Check(denied, "unknown ID fails closed");
WeaponBundle.ValidateGame(args[1]); WeaponBundle.ValidatePayload();
Check(true, "current CSOHLDS/mp/hw PE compatibility and all 13 embedded x86 DLL exports");
using (var manager = new NativeWeaponServer()) Check(!manager.ReadyFor(subset) && manager.LoadedCount == 0, "selected is not reported as running");
var settings = new LauncherSettings { GameRoot = root, EnabledWeaponIds = [726] };
var config = Path.Combine(root, "Server", "ServerConfig.json"); var asset = Path.Combine(root, "Data", "fixtrike.nar");
var original = "{\"Port\":30002,\"SSL\":false,\"Room\":{\"HostConnectingMethod\":2,\"CustomSetting\":\"preserve\"},\"DedicatedServerWhitelist\":[\"192.0.2.1\"],\"Unrelated\":{\"value\":17}}";
File.WriteAllText(config, original); File.WriteAllText(asset, "custom-pack-do-not-overwrite");
denied = false; try { WeaponAssets.Inspect(settings); } catch (InvalidDataException) { denied = true; }
Check(denied && File.ReadAllText(asset) == "custom-pack-do-not-overwrite" && File.ReadAllText(config) == original, "unknown custom asset is never overwritten");
File.Delete(asset); // exactly one file created by this fixture, not a directory
File.Copy(Path.Combine(args[1], "Server", "Documentation", "fixtrike.nar"), asset);
var plan = WeaponAssets.Inspect(settings); Check(plan.Required && plan.ConfigChanged && plan.AssetChanged, "known base yields explicit preparation plan");
File.AppendAllText(config, " "); denied = false; try { WeaponAssets.Apply(plan); } catch (IOException) { denied = true; }
Check(denied && WeaponBundle.Hash(asset) == plan.PreviousAssetHash, "concurrent config edit cancels before replacement");
File.WriteAllText(config, original); plan = WeaponAssets.Inspect(settings); var backup = WeaponAssets.Apply(plan);
Check(File.ReadAllText(Path.Combine(backup, "ServerConfig.json")) == original && WeaponBundle.Hash(Path.Combine(backup, "fixtrike.nar")) == plan.PreviousAssetHash, "exact configuration and original asset backup");
using (var doc = JsonDocument.Parse(File.ReadAllText(config)))
{
    var cfg = doc.RootElement;
    Check(cfg.GetProperty("Room").GetProperty("HostConnectingMethod").GetInt32() == 1 && cfg.GetProperty("Room").GetProperty("CustomSetting").GetString() == "preserve" && cfg.GetProperty("Unrelated").GetProperty("value").GetInt32() == 17, "only required config semantics changed");
    Check(cfg.GetProperty("DedicatedServerWhitelist").EnumerateArray().Select(v => v.GetString()).SequenceEqual(new[] { "192.0.2.1", "127.0.0.1" }), "whitelist preserves existing entries");
}
Check(WeaponBundle.Hash(asset) == WeaponBundle.Asset.Sha256 && !WeaponAssets.Inspect(settings).Required, "asset installed and preparation is idempotent");
var argsInfo = GameLauncher.CreateStartInfo(settings, "qauser1", "TestPass9!");
Check(!argsInfo.ArgumentList.Any(a => a.Contains("TestPass9!") || a.Contains(".dll", StringComparison.OrdinalIgnoreCase)), "client arguments contain neither credentials nor weapon DLLs");
Console.WriteLine($"PASS_OFFLINE_ONLY {count} weapon launcher checks; no game/server process started and no real settings/config/database changed");
