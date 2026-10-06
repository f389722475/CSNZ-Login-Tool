using static Csnz.Launcher.Localizer;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Csnz.Launcher;
public sealed record WeaponPreparation(string Root, string? ConfigPath, string AssetPath, byte[] OriginalConfig, byte[] PreparedConfig, bool ConfigChanged, bool AssetChanged, string? PreviousAssetHash)
{ public bool Required => ConfigChanged || AssetChanged; }

public static class WeaponAssets
{
    public static WeaponPreparation Inspect(LauncherSettings settings)
    {
        Multiplayer.Validate(settings);
        var root = GamePaths.NormalizeRoot(settings.GameRoot);
        var config = Path.Combine(root, "Server", "ServerConfig.json"); var target = Path.Combine(root, "Data", "fixtrike.nar");
        bool local = Multiplayer.IsHost(settings);
        var original = local ? File.ReadAllBytes(config) : [];
        byte[] prepared = original; bool changed = false;
        if (local)
        {
            var text = Encoding.UTF8.GetString(original).TrimStart('\uFEFF');
            var node = JsonNode.Parse(text, documentOptions: new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true }) as JsonObject ?? throw Error<InvalidDataException>("Weapons.ConfigFormat");
            if (node["Room"] is not JsonObject room) throw Error<InvalidDataException>("Weapons.ConfigFormat");
            changed = room["HostConnectingMethod"]?.GetValue<int>() != 1;
            room["HostConnectingMethod"] = 1;
            if (node["DedicatedServerWhitelist"] is not JsonArray whitelist) throw Error<InvalidDataException>("Weapons.ConfigFormat");
            if (!whitelist.Any(n => n?.GetValue<string>() == "127.0.0.1")) { whitelist.Add("127.0.0.1"); changed = true; }
            if (changed) prepared = Encoding.UTF8.GetBytes(node.ToJsonString(new JsonSerializerOptions(JsonSerializerOptions.Default) { WriteIndented = true }) + Environment.NewLine);
        }
        var hash = File.Exists(target) ? WeaponBundle.Hash(target) : null;
        if (hash != null && hash != WeaponBundle.Asset.Sha256 && !WeaponBundle.Asset.ReplaceableBaseHashes.Contains(hash)) throw Error<InvalidDataException>("Weapons.UnknownAsset", target);
        // No write occurs here. The window asks before Apply, and Apply rejects
        // any concurrent edit between this inspection and the actual commit.
        return new(root, local ? config : null, target, original, prepared,
            changed, hash != WeaponBundle.Asset.Sha256, hash);
    }
    public static string Apply(WeaponPreparation plan)
    {
        if (!plan.Required) return "";
        foreach (var name in new[] { "CSNZ_Server", "CSOHLDS", "CSOLauncher" })
        {
            var processes = Process.GetProcessesByName(name);
            try { if (processes.Any(p => string.Equals(ProcessTools.GetImagePath(p.Id), Path.Combine(plan.Root, name == "CSNZ_Server" ? "Server" : "Bin", name + ".exe"), StringComparison.OrdinalIgnoreCase))) throw Error<InvalidOperationException>("Weapons.PrepareWhileRunning"); }
            finally { foreach (var process in processes) process.Dispose(); }
        }
        if ((plan.ConfigPath != null && !File.ReadAllBytes(plan.ConfigPath).AsSpan().SequenceEqual(plan.OriginalConfig)) || (File.Exists(plan.AssetPath) ? WeaponBundle.Hash(plan.AssetPath) : null) != plan.PreviousAssetHash) throw Error<IOException>("Weapons.PreparationChanged");
        foreach (var path in new[] { plan.ConfigPath, plan.AssetPath }) if (path != null && File.Exists(path) && (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) throw Error<IOException>("Weapons.PreparationChanged");
        var backup = Path.Combine(plan.Root, "CSNZLauncherBackups", "weapons-" + DateTime.Now.ToString("yyyyMMdd-HHmmss-fff") + "-" + Guid.NewGuid().ToString("N")[..6]);
        Directory.CreateDirectory(backup);
        if (plan.ConfigPath != null) File.WriteAllBytes(Path.Combine(backup, "ServerConfig.json"), plan.OriginalConfig);
        if (plan.AssetChanged && plan.PreviousAssetHash != null) File.Copy(plan.AssetPath, Path.Combine(backup, "fixtrike.nar"));
        var temp = plan.AssetPath + ".csnz-" + Guid.NewGuid().ToString("N") + ".tmp";
        var configTemp = (plan.ConfigPath ?? plan.AssetPath) + ".csnz-" + Guid.NewGuid().ToString("N") + ".tmp"; bool assetWritten = false, configWritten = false;
        try
        {
            if (plan.AssetChanged)
            {
                Directory.CreateDirectory(Path.GetDirectoryName(plan.AssetPath)!); File.Copy(WeaponBundle.AssetPath, temp);
                if (WeaponBundle.Hash(temp) != WeaponBundle.Asset.Sha256) throw Error<InvalidDataException>("Weapons.BundleFormat");
            }
            if (plan.ConfigChanged) { using var output = new FileStream(configTemp, FileMode.CreateNew, FileAccess.Write, FileShare.None); output.Write(plan.PreparedConfig); output.Flush(true); }
            if ((plan.ConfigPath != null && !File.ReadAllBytes(plan.ConfigPath).AsSpan().SequenceEqual(plan.OriginalConfig)) || (File.Exists(plan.AssetPath) ? WeaponBundle.Hash(plan.AssetPath) : null) != plan.PreviousAssetHash) throw Error<IOException>("Weapons.PreparationChanged");
            if (plan.AssetChanged) { File.Move(temp, plan.AssetPath, true); assetWritten = true; }
            if (plan.ConfigChanged) { File.Move(configTemp, plan.ConfigPath!, true); configWritten = true; }
            File.WriteAllText(Path.Combine(backup, "receipt.json"), JsonSerializer.Serialize(new { schema = 1, version = WeaponBundle.Catalog.Version, config = plan.ConfigPath, asset = plan.AssetPath,
                plan.ConfigChanged, plan.AssetChanged, previousAssetHash = plan.PreviousAssetHash, installedAssetHash = WeaponBundle.Asset.Sha256, gameBinariesModified = false, databasesModified = false }, new JsonSerializerOptions { WriteIndented = true }));
            return backup;
        }
        catch
        {
            if (configWritten) File.WriteAllBytes(plan.ConfigPath!, plan.OriginalConfig);
            if (assetWritten) { if (plan.PreviousAssetHash == null) File.Delete(plan.AssetPath); else File.Copy(Path.Combine(backup, "fixtrike.nar"), plan.AssetPath, true); }
            throw;
        }
        finally { if (File.Exists(temp)) File.Delete(temp); if (File.Exists(configTemp)) File.Delete(configTemp); }
    }
}
