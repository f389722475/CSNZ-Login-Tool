using static Csnz.Launcher.Localizer;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;

namespace Csnz.Launcher;

public sealed record WeaponDescriptor(int Id, string Name, string Folder, string Family, bool Repair)
{
    public string Detail => $"ID {Id}  /  {Family}" + (Repair ? "" : "  /  REFERENCE");
    public string RelativeDll => $"{Folder}/{Folder}.dll";
}
public sealed record WeaponCatalog(int Schema, string Version, uint Api, string Scope, string Core, WeaponDescriptor[] Weapons);
public sealed record WeaponBundleFile(string Path, long Bytes, string Sha256);
public sealed record WeaponBundleManifest(int Schema, string Version, string Architecture, string Implementation, WeaponBundleFile[] Files);
public sealed record WeaponAssetManifest(int Schema, string Path, long Bytes, string Sha256, string[] ReplaceableBaseHashes, int Entries, string Distribution);

public static class WeaponBundle
{
    private static readonly JsonSerializerOptions Json = new() { PropertyNameCaseInsensitive = true };
    private static T Read<T>(string name) { using var input = Resource(name); return JsonSerializer.Deserialize<T>(input, Json) ?? throw new InvalidDataException(name); }
    private static Stream Resource(string name) => typeof(WeaponBundle).Assembly.GetManifestResourceStream("WeaponMods." + name)
        ?? throw Error<InvalidDataException>("Error.BundleMissing", name);
    public static WeaponCatalog Catalog { get; } = Read<WeaponCatalog>("catalog.json");
    public static WeaponBundleManifest Manifest { get; } = Read<WeaponBundleManifest>("bundle-manifest.json");
    public static WeaponAssetManifest Asset { get; } = Read<WeaponAssetManifest>("asset-manifest.json");
    private static readonly Lazy<string> Payload = new(Extract);
    public static string DirectoryPath => Payload.Value;
    public static string CoreDll => Path.Combine(DirectoryPath, Catalog.Core.Replace('/', Path.DirectorySeparatorChar));
    public static string AssetPath => Path.Combine(DirectoryPath, Asset.Path.Replace('/', Path.DirectorySeparatorChar));
    public static string Dll(WeaponDescriptor weapon) => Path.Combine(DirectoryPath, weapon.RelativeDll.Replace('/', Path.DirectorySeparatorChar));
    public static int[] DefaultIds => Catalog.Weapons.Where(w => w.Repair).Select(w => w.Id).ToArray();
    public static WeaponDescriptor[] Selected(LauncherSettings settings)
    {
        var ids = settings.EnabledWeaponIds ?? (settings.EnableNativePatch ? DefaultIds : []);
        ids = ids.Where(id => id != 537).ToArray(); // retire the reference without dropping repaired selections
        var unknown = ids.Except(Catalog.Weapons.Select(w => w.Id)).ToArray();
        if (unknown.Length != 0) throw Error<InvalidDataException>("Weapons.UnknownIds", string.Join(", ", unknown));
        return Catalog.Weapons.Where(w => ids.Contains(w.Id)).ToArray();
    }
    public static string Hash(string file) { using var input = File.OpenRead(file); return Convert.ToHexString(SHA256.HashData(input)).ToLowerInvariant(); }
    private static string SafePath(string root, string relative)
    {
        var path = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
        if (Path.IsPathRooted(relative) || !path.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Invalid bundle path");
        return path;
    }
    private static string Extract()
    {
        if (Catalog.Schema != 1 || Manifest.Schema != 1 || Catalog.Api != 65536 || Manifest.Version != Catalog.Version || Manifest.Architecture != "x86" || Manifest.Implementation != "native-cpp") throw Error<InvalidDataException>("Weapons.BundleFormat");
        var expected = Catalog.Weapons.Select(w => w.RelativeDll).Append(Catalog.Core).Order().ToArray();
        if (Catalog.Weapons.Select(w => w.Id).Distinct().Count() != Catalog.Weapons.Length || !expected.SequenceEqual(Manifest.Files.Select(f => f.Path).Order())) throw Error<InvalidDataException>("Weapons.BundleFormat");
        var identity = string.Join("\n", Manifest.Files.OrderBy(f => f.Path).Select(f => f.Path + ":" + f.Sha256)) + "\n" + Asset.Sha256;
        var fingerprint = Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(identity)))[..12].ToLowerInvariant();
        var root = Path.GetFullPath(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "CSNZLauncher", "Native", "Weapons", Catalog.Version + "-" + fingerprint));
        Directory.CreateDirectory(root);
        foreach (var file in Manifest.Files.Append(new WeaponBundleFile(Asset.Path, Asset.Bytes, Asset.Sha256)))
        {
            var target = SafePath(root, file.Path); Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            if (!File.Exists(target))
            {
                var temporary = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
                try
                {
                    using (var input = Resource(Path.GetFileName(file.Path)))
                    using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { input.CopyTo(output); output.Flush(true); }
                    if (new FileInfo(temporary).Length != file.Bytes || Hash(temporary) != file.Sha256) throw Error<InvalidDataException>("Error.BundleMismatch", root);
                    try { File.Move(temporary, target); } catch (IOException) when (File.Exists(target)) { }
                }
                finally { if (File.Exists(temporary)) File.Delete(temporary); }
            }
            if (new FileInfo(target).Length != file.Bytes || Hash(target) != file.Sha256) throw Error<InvalidDataException>("Error.BundleMismatch", root);
        }
        return root;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")] private static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)] private static extern IntPtr GetProcAddress(IntPtr module, string name);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate uint Query(IntPtr argument);
    private static void ValidateDll(string path, string[] exports, string apiName, int? id = null)
    {
        if (IntPtr.Size != 4) throw Error<InvalidOperationException>("Error.Launcher32");
        var module = LoadLibraryEx(path, IntPtr.Zero, 0x100 | 0x800);
        if (module == IntPtr.Zero) throw Error<InvalidDataException>("Weapons.DllLoad", path, Marshal.GetLastWin32Error());
        try
        {
            foreach (var name in exports) if (GetProcAddress(module, name) == IntPtr.Zero) throw Error<InvalidDataException>("Native.Exports");
            uint Call(string name) => Marshal.GetDelegateForFunctionPointer<Query>(GetProcAddress(module, name))(IntPtr.Zero);
            if (Call(apiName) != Catalog.Api || id.HasValue && Call("CSNZWeapon_Id") != id.Value) throw Error<InvalidDataException>("Weapons.BundleFormat");
        }
        finally { FreeLibrary(module); }
    }
    public static void ValidatePayload()
    {
        ValidateDll(CoreDll, ["CSNZWeapons_Api", "CSNZWeapons_Register", "CSNZWeapons_Start", "CSNZWeapons_Stop", "CSNZWeapons_Status", "CSNZWeapons_IsClean"], "CSNZWeapons_Api");
        foreach (var weapon in Catalog.Weapons) ValidateDll(Dll(weapon), ["CSNZWeapon_Api", "CSNZWeapon_Id", "CSNZWeapon_Start"], "CSNZWeapon_Api", weapon.Id);
    }
    public static void ValidateGame(string gameRoot)
    {
        foreach (var (name, hash) in new[] {
            ("CSOHLDS.exe", "a2ce29976618699a408da0b21d97ed9eddf220d221254ab4265d90549ac26c6f"),
            ("mp.dll", "9680d98c306ef3c1c606e1b22c1bd0123ca861bd0cfb86b25b4da009327e0686"),
            ("hw.dll", "52345cbe9b52f75b1c1da70254c718d78f2eff746ac8397d10251a9d134a32e9") })
        {
            var path = Path.Combine(GamePaths.NormalizeRoot(gameRoot), "Bin", name);
            if (!File.Exists(path) || Hash(path) != hash) throw Error<InvalidDataException>("Native.BuildMismatch", name);
        }
    }
}
