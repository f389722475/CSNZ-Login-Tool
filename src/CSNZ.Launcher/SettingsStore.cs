using static Csnz.Launcher.Localizer;
using System.IO;
using System.Text.Json;
using System.Text;
using System.Runtime.InteropServices;
using System.ComponentModel;
using System.Security.Cryptography;

namespace Csnz.Launcher;
public sealed class LauncherSettings
{
    public string Language { get; set; } = "zh-CN";
    public string GameRoot { get; set; } = "";
    public bool GameRootIsManual { get; set; }
    public string Host { get; set; } = "127.0.0.1";
    public int Port { get; set; } = 30002;
    public bool UseTls { get; set; }
    public bool StartLocalServer { get; set; } = true;
    public bool EnableNativePatch { get; set; } = true;
    public bool MinimizeOnLaunch { get; set; }
    public bool RememberAccount { get; set; } = true;
    public bool RememberPassword { get; set; }
    public string Account { get; set; } = "";
    public string ProtectedPassword { get; set; } = "";
    public string CredentialBinding => $"CSNZ.Desktop.v1|{Host.ToLowerInvariant()}|{Port}|{UseTls}|{Account}";
}

public static class SettingsStore
{
    public static string DirectoryPath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "CSNZLauncher");
    public static string FilePath => Path.Combine(DirectoryPath, "settings.json");
    public static LauncherSettings Load(out UiText? warning)
    {
        warning = null;
        if (!File.Exists(FilePath)) return ResolveGameRoot(new(), AppContext.BaseDirectory);
        try { return ResolveGameRoot(JsonSerializer.Deserialize<LauncherSettings>(File.ReadAllText(FilePath)) ?? new(), AppContext.BaseDirectory); }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
        { warning = Msg("Settings.Unreadable"); return ResolveGameRoot(new(), AppContext.BaseDirectory); }
    }
    public static LauncherSettings ResolveGameRoot(LauncherSettings settings, string executableDirectory)
    {
        settings.Language = Normalize(settings.Language);
        settings.GameRoot = GamePaths.ResolveRoot(settings.GameRoot, settings.GameRootIsManual, executableDirectory);
        return settings;
    }
    public static void SaveLanguage(LauncherSettings settings, string language, string? path = null)
    {
        string previous = settings.Language;
        settings.Language = Normalize(language);
        try { Save(settings, path); }
        catch { settings.Language = previous; throw; }
    }
    public static void Save(LauncherSettings settings, string? path = null)
    {
        path ??= FilePath;
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var json = JsonSerializer.Serialize(settings, new JsonSerializerOptions { WriteIndented = true });
        var temporary = path + ".tmp";
        File.WriteAllText(temporary, json, new UTF8Encoding(false));
        File.Move(temporary, path, true);
    }
}

// Windows DPAPI CurrentUser: no machine-wide flag, no plaintext password files.
public static class SecretStore
{
    [StructLayout(LayoutKind.Sequential)] private struct Blob { public int Length; public IntPtr Data; }
    [DllImport("crypt32.dll", SetLastError = true, CharSet = CharSet.Unicode)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptProtectData(ref Blob input, string? description, ref Blob entropy, IntPtr reserved, IntPtr prompt, uint flags, out Blob output);
    [DllImport("crypt32.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptUnprotectData(ref Blob input, IntPtr description, ref Blob entropy, IntPtr reserved, IntPtr prompt, uint flags, out Blob output);
    [DllImport("kernel32.dll")] private static extern IntPtr LocalFree(IntPtr memory);
    public static string Protect(string password, string binding)
    {
        var bytes = Encoding.UTF8.GetBytes(password);
        try { return Convert.ToBase64String(Transform(bytes, binding, true)); }
        finally { CryptographicOperations.ZeroMemory(bytes); }
    }
    public static string Unprotect(string value, string binding)
    {
        var bytes = Transform(Convert.FromBase64String(value), binding, false);
        try { return Encoding.UTF8.GetString(bytes); }
        finally { CryptographicOperations.ZeroMemory(bytes); }
    }
    private static byte[] Transform(byte[] input, string binding, bool protect)
    {
        var entropyBytes = Encoding.UTF8.GetBytes(binding);
        var a = new Blob { Length = input.Length, Data = Marshal.AllocHGlobal(input.Length) };
        var b = new Blob { Length = entropyBytes.Length, Data = Marshal.AllocHGlobal(entropyBytes.Length) };
        Blob output = default;
        try
        {
            Marshal.Copy(input, 0, a.Data, input.Length); Marshal.Copy(entropyBytes, 0, b.Data, entropyBytes.Length);
            var ok = protect ? CryptProtectData(ref a, "CSNZ Launcher", ref b, IntPtr.Zero, IntPtr.Zero, 1, out output)
                             : CryptUnprotectData(ref a, IntPtr.Zero, ref b, IntPtr.Zero, IntPtr.Zero, 1, out output);
            if (!ok) { int code = Marshal.GetLastWin32Error(); throw WithText(new Win32Exception(code, Text("Error.Dpapi", code)), Msg("Error.Dpapi", code)); }
            var result = new byte[output.Length]; Marshal.Copy(output.Data, result, 0, result.Length); return result;
        }
        finally
        {
            for (int i = 0; i < a.Length; i++) Marshal.WriteByte(a.Data, i, 0);
            Marshal.FreeHGlobal(a.Data); Marshal.FreeHGlobal(b.Data);
            if (output.Data != IntPtr.Zero) { for (int i = 0; i < output.Length; i++) Marshal.WriteByte(output.Data, i, 0); LocalFree(output.Data); }
        }
    }
}
